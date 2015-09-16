/*******************************************************************************
 * thrill/api/groupby.hpp
 *
 * DIANode for a groupby to indx operation.
 * Performs the actual groupby operation
 *
 * Part of Project Thrill.
 *
 * Copyright (C) 2015 Huyen Chau Nguyen <hello@chau-nguyen.de>
 *
 * This file has no license. Only Chuck Norris can compile it.
 ******************************************************************************/

#pragma once
#ifndef THRILL_API_GROUPBY_INDEX_HEADER
#define THRILL_API_GROUPBY_INDEX_HEADER

#include <thrill/api/dia.hpp>
#include <thrill/api/dop_node.hpp>
#include <thrill/api/groupby_iterator.hpp>
#include <thrill/common/functional.hpp>
#include <thrill/common/logger.hpp>
#include <thrill/core/iterator_wrapper.hpp>
#include <thrill/core/multiway_merge.hpp>

#include <functional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace thrill {
namespace api {

template <typename ValueType, typename ParentDIARef,
          typename KeyExtractor, typename GroupFunction, typename HashFunction>
class GroupByIndexNode : public DOpNode<ValueType>
{
    static const bool debug = false;
    using Super = DOpNode<ValueType>;
    using Key = typename common::FunctionTraits<KeyExtractor>::result_type;
    using ValueOut = ValueType;
    using GroupIterator = typename common::FunctionTraits<GroupFunction>
                          ::template arg<0>;
    using ValueIn = typename common::FunctionTraits<KeyExtractor>
                    ::template arg<0>;

    using File = data::File;
    using Reader = typename File::Reader;
    using Writer = typename File::Writer;

    struct ValueComparator
    {
        ValueComparator(const GroupByIndexNode& info) : info_(info) { }
        const GroupByIndexNode& info_;

        bool operator () (const ValueType& i,
                          const ValueType& j) {
            auto i_cmp = info_.hash_function_(info_.key_extractor_(i));
            auto j_cmp = info_.hash_function_(info_.key_extractor_(j));
            return (i_cmp < j_cmp);
        }
    };

    using Super::context_;

public:
    /*!
     * Constructor for a GroupByIndexNode. Sets the DataManager, parent, stack,
     * key_extractor and reduce_function.
     *
     * \param parent Parent DIARef.
     * and this node
     * \param key_extractor Key extractor function
     * \param reduce_function Reduce function
     */
    GroupByIndexNode(const ParentDIARef& parent,
                KeyExtractor key_extractor,
                GroupFunction groupby_function,
                std::size_t number_keys,
                ValueOut neutral_element,
                StatsNode* stats_node,
                const HashFunction& hash_function = HashFunction())
        : DOpNode<ValueType>(parent.ctx(), { parent.node() }, stats_node),
          key_extractor_(key_extractor),
          groupby_function_(groupby_function),
          number_keys_(number_keys),
          neutral_element_(neutral_element),
          hash_function_(hash_function),
          channel_(parent.ctx().GetNewChannel()),
          emitter_(channel_->OpenWriters()),
          sorted_elems_(context_.GetFile())
    {
        // Hook PreOp
        auto pre_op_fn = [=](const ValueIn& input) {
                             PreOp(input);
                         };
        // close the function stack with our pre op and register it at
        // parent node for output
        auto lop_chain = parent.stack().push(pre_op_fn).emit();
        parent.node()->RegisterChild(lop_chain, this->type());
        channel_->OnClose([this]() {
                              this->WriteChannelStats(this->channel_);
                          });
    }

    //! Virtual destructor for a GroupByIndexNode.
    virtual ~GroupByIndexNode() { }

    /*!
     * Actually executes the reduce operation. Uses the member functions PreOp,
     * MainOp and PostOp.
     */
    void Execute() override {
        MainOp();
    }

    void PushData(bool consume) final {
        Reader r = sorted_elems_.GetReader(consume);
        if (r.HasNext()) {
            // create iterator to pass to user_function
            auto user_iterator = GroupByIterator<ValueIn, KeyExtractor>(r, key_extractor_);
            std::size_t keyspace = number_keys_ / emitter_.size() + (number_keys_ % emitter_.size() != 0);
            std::size_t curr_index = keyspace * context_.my_rank();

            while (user_iterator.HasNextForReal()) {
                if (user_iterator.GetNextKey() != curr_index) {
                    // push neutral element as result to callback functions
                    for (auto func : DIANode<ValueType>::callbacks_) {
                        func(neutral_element_);
                    }
                } else {
                    //call user function
                    // TODO(cn): call groupby function while doing multiway merge
                    const ValueOut res = groupby_function_(user_iterator,
                        user_iterator.GetNextKey());
                    // push result to callback functions
                    for (auto func : DIANode<ValueType>::callbacks_) {
                        LOG << "grouped to value " << res;
                        func(res);
                    }
                }
                ++curr_index;
            }
        }
    }

    void Dispose() override { }

    /*!
     * Produces a function stack, which only contains the PostOp function.
     * \return PostOp function stack
     */
    auto ProduceStack() {
        return FunctionStack<ValueType>();
    }

private:
    KeyExtractor key_extractor_;
    GroupFunction groupby_function_;
    std::size_t number_keys_;
    ValueOut neutral_element_;
    HashFunction hash_function_;

    data::ChannelPtr channel_;
    std::vector<data::Channel::Writer> emitter_;
    std::vector<data::File> files_;
    data::File sorted_elems_;

    /*
     * Send all elements to their designated PEs
     */
    void PreOp(const ValueIn& v) {
        const Key k = key_extractor_(v);
        assert(k < number_keys_);
        const auto recipient = k /
                               (number_keys_ / emitter_.size()
                                + (number_keys_ % emitter_.size() != 0)); //round up
        LOG << "sending " << v
            << " with key " << k
            << " to " << recipient << "/" << emitter_.size();
        emitter_[recipient](v);
    }

    /*
     * Sort and store elements in a file
     */
    void FlushVectorToFile(std::vector<ValueIn>& v) {
        // sort run and sort to file
        std::sort(v.begin(), v.end(), ValueComparator(*this));
        File f = context_.GetFile();
        {
            Writer w = f.GetWriter();
            for (const ValueIn& e : v) {
                w(e);
            }
            w.Close();
        }

        files_.push_back(f);
    }

    //! Receive elements from other workers.
    auto MainOp() {
        using Iterator = thrill::core::FileIteratorWrapper<ValueIn>;
        using OIterator = thrill::core::FileOutputIteratorWrapper<ValueIn>;

        LOG << "running group by main op";

        const bool consume = true;
        const std::size_t FIXED_VECTOR_SIZE = 1000000000 / sizeof(ValueIn);
        std::vector<ValueIn> incoming;
        incoming.reserve(FIXED_VECTOR_SIZE);

        // close all emitters
        for (auto& e : emitter_) {
            e.Close();
        }

        std::size_t totalsize = 0;

        // get incoming elements
        auto reader = channel_->OpenConcatReader(consume);
        while (reader.HasNext()) {
            // if vector is full save to disk
            if (incoming.size() == FIXED_VECTOR_SIZE) {
                totalsize += FIXED_VECTOR_SIZE;
                FlushVectorToFile(incoming);
                incoming.clear();
            }
            // store incoming element
            const ValueIn elem = reader.template Next<ValueIn>();
            incoming.push_back(elem);
        }
        totalsize += incoming.size();
        FlushVectorToFile(incoming);
        std::vector<ValueIn>().swap(incoming);

        const std::size_t num_runs = files_.size();

        // if there's only one run, store it
        if (num_runs == 1) {
            Writer w = sorted_elems_.GetWriter();
            Reader r = files_[0].GetReader(consume);
            {
                while (r.HasNext()) {
                    w(r.template Next<ValueIn>());
                }
            }
        }       // otherwise sort all runs using multiway merge
        else {
            std::vector<std::pair<Iterator, Iterator> > seq;
            seq.reserve(num_runs);
            for (std::size_t t = 0; t < num_runs; ++t) {
                std::shared_ptr<Reader> reader = std::make_shared<Reader>(files_[t].GetReader(consume));
                Iterator s = Iterator(&files_[t], reader, 0, true);
                Iterator e = Iterator(&files_[t], reader, files_[t].num_items(), false);
                seq.push_back(std::make_pair(std::move(s), std::move(e)));
            }

            {
                OIterator oiter(std::make_shared<Writer>(sorted_elems_.GetWriter()));

                core::sequential_file_multiway_merge<true, false>(
                    std::begin(seq),
                    std::end(seq),
                    oiter,
                    totalsize,
                    ValueComparator(*this));
            }
        }
    }
};

/******************************************************************************/

template <typename ValueType, typename Stack>
template <typename KeyExtractor,
          typename GroupFunction,
          typename ValueOut,
          typename HashFunction>
auto DIARef<ValueType, Stack>::GroupByIndex(
    const KeyExtractor &key_extractor,
    const GroupFunction &groupby_function,
    const std::size_t number_keys,
    const ValueOut neutral_element) const {

    using DOpResult
              = typename common::FunctionTraits<GroupFunction>::result_type;

    static_assert(
        std::is_same<
            typename std::decay<typename common::FunctionTraits<KeyExtractor>
                                ::template arg<0> >::type,
            ValueType>::value,
        "KeyExtractor has the wrong input type");

    StatsNode* stats_node = AddChildStatsNode("GroupByIndex", DIANodeType::DOP);
    using GroupByResultNode
              = GroupByIndexNode<DOpResult, DIARef, KeyExtractor,
                            GroupFunction, HashFunction>;
    auto shared_node
        = std::make_shared<GroupByResultNode>(*this,
                                              key_extractor,
                                              groupby_function,
                                              number_keys,
                                              neutral_element,
                                              stats_node);

    auto groupby_stack = shared_node->ProduceStack();

    return DIARef<DOpResult, decltype(groupby_stack)>(
        shared_node,
        groupby_stack,
        { stats_node });
}

} // namespace api
} // namespace thrill

#endif // !THRILL_API_GROUPBY_INDEX_HEADER

/******************************************************************************/
