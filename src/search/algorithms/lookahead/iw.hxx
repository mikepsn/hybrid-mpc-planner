
#pragma once

#include <stdio.h>
#include <unordered_set>



#include <problem.hxx>
#include <problem_info.hxx>
#include <search/drivers/sbfws/base.hxx>
#include <search/drivers/sbfws/stats.hxx>
#include <utils/printers/vector.hxx>
#include <utils/printers/actions.hxx>
#include <lapkt/search/components/open_lists.hxx>
#include <utils/config.hxx>
#include <lapkt/novelty/tuples.hxx>
#include <lapkt/novelty/features.hxx>
#include <lapkt/tools/resources_control.hxx>
#include <lapkt/tools/logging.hxx>
#include <heuristics/novelty/features.hxx>
#include <heuristics/novelty/goal_ball_filter.hxx>

// For writing the R sets
#include <utils/archive/json.hxx>

namespace fs0 { namespace lookahead {

using FSFeatureValueT = lapkt::novelty::FeatureValueT;
typedef lapkt::novelty::Width1Tuple<FSFeatureValueT> Width1Tuple;
typedef lapkt::novelty::Width1TupleHasher<FSFeatureValueT> Width1TupleHasher;
typedef lapkt::novelty::Width2Tuple<FSFeatureValueT> Width2Tuple;
typedef lapkt::novelty::Width2TupleHasher<FSFeatureValueT> Width2TupleHasher;

template <typename StateT, typename ActionType>
class IWNode {
public:
	using ActionT = ActionType;
	using PT = std::shared_ptr<IWNode<StateT, ActionT>>;

	//! The state in this node
	StateT state;

	//! The action that led to this node
	typename ActionT::IdType action;

	//! The parent node
	PT parent;

	//! Accummulated cost
	unsigned g;

	//! The novelty  of the state
	unsigned char _w;

    //! Reward
    float R;

	//! The generation order, uniquely identifies the node
	//! NOTE We're assuming we won't generate more than 2^32 ~ 4.2 billion nodes.
	uint32_t _gen_order;


	IWNode() = default;
	~IWNode() = default;
	IWNode(const IWNode&) = default;
	IWNode(IWNode&&) = delete;
	IWNode& operator=(const IWNode&) = delete;
	IWNode& operator=(IWNode&&) = delete;

	//! Constructor with full copying of the state (expensive)
	IWNode(const StateT& s, unsigned long gen_order) : IWNode(StateT(s), ActionT::invalid_action_id, nullptr, gen_order) {}

	//! Constructor with move of the state (cheaper)
	IWNode(StateT&& _state, typename ActionT::IdType _action, PT _parent, uint32_t gen_order) :
		state(std::move(_state)),
		action(_action),
		parent(_parent),
		g(parent ? parent->g+1 : 0),
		_w(std::numeric_limits<unsigned char>::max()),
        R(0.0f),
		_gen_order(gen_order)
	{
		assert(_gen_order > 0); // Very silly way to detect overflow, in case we ever generate > 4 billion nodes :-)
	}


	bool has_parent() const { return parent != nullptr; }

	//! Print the node into the given stream
	friend std::ostream& operator<<(std::ostream &os, const IWNode<StateT, ActionT>& object) { return object.print(os); }
	std::ostream& print(std::ostream& os) const {
		os << "{@ = " << this;
		os << ", #=" << _gen_order ;
		os << ", s = " << state ;
		os << ", g=" << g ;
        os << ", R=" << R ;
		os << ", w=" << (_w == std::numeric_limits<unsigned char>::max() ? "INF" : std::to_string(_w));

		os << ", act=" << action ;
		os << ", parent = " << (parent ? "#" + std::to_string(parent->_gen_order) : "None");
		return os;
	}

	bool operator==( const IWNode<StateT, ActionT>& o ) const { return state == o.state; }

	std::size_t hash() const { return state.hash(); }
};


template <typename NodeT, typename FeatureSetT, typename NoveltyEvaluatorT>
class LazyEvaluator {
protected:
	//! The set of features used to compute the novelty
	const FeatureSetT& _features;

	//! A single novelty evaluator will be in charge of evaluating all nodes
	std::unique_ptr<NoveltyEvaluatorT> _evaluator;

public:

    typedef typename NoveltyEvaluatorT::ValuationT ValuationT;

	LazyEvaluator(const FeatureSetT& features, NoveltyEvaluatorT* evaluator) :
		_features(features),
		_evaluator(evaluator)
	{}

	~LazyEvaluator() = default;

	//! Returns false iff we want to prune this node during the search
	unsigned evaluate(NodeT& node) {
		if (node.parent) {
			// Important: the novel-based computation works only when the parent has the same novelty type and thus goes against the same novelty tables!!!
			node._w = _evaluator->evaluate(_features.evaluate(node.state), _features.evaluate(node.parent->state));
		} else {
			node._w = _evaluator->evaluate(_features.evaluate(node.state));
		}

		return node._w;
	}

	std::vector<Width1Tuple> reached_tuples() const {
		std::vector<Width1Tuple> tuples;
		_evaluator->mark_tuples_in_novelty1_table(tuples);
		return tuples;
	}

	void reset() {
		_evaluator->reset();
	}

    const FeatureSetT& feature_set() const { return _features; }

};


//! A single IW run (with parametrized max. width) that runs until (independent)
//! satisfaction of each of the provided goal atoms, and computes the set
//! of atoms R that is relevant for the achievement of at least one atom.
//! R is computed treating the actions as a black-box. For this, an atom is considered
//! relevant for a certain goal atom if that atom is true in at least one of the states
//! that lies on the path between the seed node and the first node where that goal atom is
//! satisfied.
template <typename NodeT,
          typename StateModel,
          typename NoveltyEvaluatorT,
		  typename FeatureSetT
>
class IW
{
public:
	using ActionT = typename StateModel::ActionType;
	using StateT = typename StateModel::StateT;

	using ActionIdT = typename StateModel::ActionType::IdType;
    using PlanT =  std::vector<ActionIdT>;
	using NodePT = std::shared_ptr<NodeT>;

	using SimEvaluatorT = LazyEvaluator<NodeT, FeatureSetT, NoveltyEvaluatorT>;

	using FeatureValueT = typename NoveltyEvaluatorT::FeatureValueT;

	using OpenListT = lapkt::SimpleQueue<NodeT>;

	struct Config {
		//! Whether to perform a complete run or a partial one, i.e. up until (independent) satisfaction of all goal atoms.
		bool _complete;

		//! The maximum levels of novelty to be considered
		unsigned _max_width;

		//!
		const fs0::Config& _global_config;

		//! Whether to extract goal-informed relevant sets R
		bool _goal_directed;

		//! Enforce state constraints
		bool _enforce_state_constraints;

		//! Load R set from file
		std::string _R_file;

		//! Goal Ball filtering
		bool _filter_R_set;

		//! Log search
		bool _log_search;

		Config(bool complete, unsigned max_width, const fs0::Config& global_config) :
			_complete(complete),
			_max_width(max_width),
			_global_config(global_config),
			_goal_directed(global_config.getOption<bool>("lookahead.iw.goal_directed", false)),
			_enforce_state_constraints(global_config.getOption<bool>("lookahead.iw.enforce_state_constraints", true)),
			_R_file(global_config.getOption<std::string>("lookahead.iw.from_file", "")),
			_filter_R_set(global_config.getOption<bool>("lookahead.iw.filter", false)),
			_log_search(global_config.getOption<bool>("lookahead.iw.log", false))
		{}
	};

protected:
	//! The search model
	const StateModel& _model;

	//! The simulation configuration
	Config _config;

    //! Best node found
	NodePT _best_node;

	//!
	std::vector<NodePT> _optimal_paths;

	//! '_unreached' contains the indexes of all those goal atoms that have yet not been reached.
	std::unordered_set<unsigned> _unreached;

	//! Contains the indexes of all those goal atoms that were already reached in the seed state
	std::vector<bool> _in_seed;

	//! A single novelty evaluator will be in charge of evaluating all nodes
	SimEvaluatorT _evaluator;

	//! Some node counts
	uint32_t _generated;
	uint32_t _w1_nodes_expanded;
	uint32_t _w2_nodes_expanded;
	uint32_t _w1_nodes_generated;
	uint32_t _w2_nodes_generated;
	uint32_t _w_gt2_nodes_generated;

	//! The general statistics of the search
	bfws::BFWSStats& _stats;

	//! Whether to print some useful extra information or not
	bool _verbose;

	// MRJ: IW(1) debugging
	std::vector<NodePT>	_visited;

public:

	//! Constructor
	IW(const StateModel& model, const FeatureSetT& featureset, NoveltyEvaluatorT* evaluator, const IW::Config& config, bfws::BFWSStats& stats, bool verbose) :
		_model(model),
		_config(config),
        _best_node(nullptr),
		_optimal_paths(model.num_subgoals()),
		_unreached(),
		_in_seed(),
		_evaluator(featureset, evaluator),
		_generated(1),
		_w1_nodes_expanded(0),
		_w2_nodes_expanded(0),
		_w1_nodes_generated(0),
		_w2_nodes_generated(0),
		_w_gt2_nodes_generated(0),
		_stats(stats),
		_verbose(verbose)
	{
	}

	void reset() {
		std::vector<NodePT> _(_optimal_paths.size(), nullptr);
		_optimal_paths.swap(_);
		_generated = 1;
		_w1_nodes_expanded = 0;
		_w2_nodes_expanded = 0;
		_w1_nodes_generated = 0;
		_w2_nodes_generated = 0;
		_w_gt2_nodes_generated = 0;
        _best_node = nullptr;
		_evaluator.reset();
	}

	~IW() = default;

	// Disallow copy, but allow move
	IW(const IW&) = delete;
	IW(IW&&) = default;
	IW& operator=(const IW&) = delete;
	IW& operator=(IW&&) = default;

	void report_simulation_stats(float simt0) {
		_stats.simulation();
		_stats.sim_add_time(aptk::time_used() - simt0);
		_stats.sim_add_expanded_nodes(_w1_nodes_expanded+_w2_nodes_expanded);
		_stats.sim_add_generated_nodes(_w1_nodes_generated+_w2_nodes_generated+_w_gt2_nodes_generated);
		_stats.reachable_subgoals( _model.num_subgoals() - _unreached.size());
	}

	std::vector<NodePT> extract_seed_nodes() {
		std::vector<NodePT> seed_nodes;
		for (unsigned subgoal_idx = 0; subgoal_idx < _optimal_paths.size(); ++subgoal_idx) {
			if (!_in_seed[subgoal_idx] && _optimal_paths[subgoal_idx] != nullptr) {
				seed_nodes.push_back(_optimal_paths[subgoal_idx]);
			}
		}
		return seed_nodes;
	}

	class DeactivateZCC {
		bool _current_setting;
	public:
		DeactivateZCC() {
			_current_setting = fs0::Config::instance().getZeroCrossingControl();
			fs0::Config::instance().setZeroCrossingControl(false);
		}

		~DeactivateZCC() {
			fs0::Config::instance().setZeroCrossingControl(_current_setting);
		}
	};

    //! Convenience method
	bool solve_model(PlanT& solution) { return search(_model.init(), solution); }

	bool search(const StateT& s, PlanT& plan) {
        _best_node = nullptr; // Make sure we start assuming no solution found

        run(s, _config._max_width);


		return extract_plan( _best_node, plan);
	}

    //! Returns true iff there is an actual plan (i.e. because the given solution node is non-null)
    bool extract_plan(const NodePT& solution_node, PlanT& plan) const {
        if (!solution_node) return false;
        assert(plan.empty());

        NodePT node = solution_node;

        while (node->parent) {
            plan.push_back(node->action);
            node = node->parent;
        }

        std::reverse(plan.begin(), plan.end());
        return true;
    }


	bool run(const StateT& seed, unsigned max_width) {
		if (_verbose) LPT_INFO("cout", "Simulation - Starting IW Simulation");

		std::shared_ptr<DeactivateZCC> zcc_setting = nullptr;
		if (!_config._enforce_state_constraints ) {
			LPT_INFO("cout", ":Simulation - Deactivating zero crossing control");
			zcc_setting = std::make_shared<DeactivateZCC>();
		}

		NodePT root = std::make_shared<NodeT>(seed, _generated++);
		mark_seed_subgoals(root);

		auto nov =_evaluator.evaluate(*root);
		assert(nov==1);
		update_novelty_counters_on_generation(nov);

// 		LPT_DEBUG("cout", "Simulation - Seed node: " << *root);

		assert(max_width <= 2); // The current swapping-queues method works only for up to width 2, but is trivial to generalize if necessary

        _best_node = root;
		OpenListT open_w1, open_w2;
		OpenListT open_w1_next, open_w2_next; // The queues for the next depth level.

		open_w1.insert(root);

		while (true) {
			while (!open_w1.empty() || !open_w2.empty()) {
				NodePT current = open_w1.empty() ? open_w2.next() : open_w1.next();

				// Expand the node
				update_novelty_counters_on_expansion(current->_w);

				for (const auto& a : _model.applicable_actions(current->state, _config._enforce_state_constraints)) {
					StateT s_a = _model.next( current->state, a );
					NodePT successor = std::make_shared<NodeT>(std::move(s_a), a, current, _generated++);

					unsigned char novelty = _evaluator.evaluate(*successor);
					update_novelty_counters_on_generation(novelty);

					// LPT_INFO("cout", "Simulation - Node generated: " << *successor);
					if (_config._log_search )
						_visited.push_back(successor);


					if (process_node(successor)) {  // i.e. all subgoals have been reached before reaching the bound
						report("All subgoals reached");
						return true;
					}

					if (novelty <= max_width && novelty == 1) open_w1_next.insert(successor);
					else if (novelty <= max_width && novelty == 2) open_w2_next.insert(successor);
				}

			}
			// We've processed all nodes in the current depth level.
			open_w1.swap(open_w1_next);
			open_w2.swap(open_w2_next);

			if (open_w1.empty() && open_w2.empty()) break;
		}

		report("State space exhausted");
		return false;
	}

	void update_novelty_counters_on_expansion(unsigned char novelty) {
		if (novelty == 1) ++_w1_nodes_expanded;
		else if (novelty== 2) ++_w2_nodes_expanded;
	}

	void update_novelty_counters_on_generation(unsigned char novelty) {
		if (novelty==1) ++_w1_nodes_generated;
		else if (novelty==2)  ++_w2_nodes_generated;
		else ++_w_gt2_nodes_generated;
	}

	void report(const std::string& result) const {
		if (!_verbose) return;
		LPT_INFO("cout", "Simulation - Result: " << result);
		LPT_INFO("cout", "Simulation - Num reached subgoals: " << (_model.num_subgoals() - _unreached.size()) << " / " << _model.num_subgoals());
		LPT_INFO("cout", "Simulation - Expanded nodes with w=1 " << _w1_nodes_expanded);
		LPT_INFO("cout", "Simulation - Expanded nodes with w=2 " << _w2_nodes_expanded);
		LPT_INFO("cout", "Simulation - Generated nodes with w=1 " << _w1_nodes_generated);
		LPT_INFO("cout", "Simulation - Generated nodes with w=2 " << _w2_nodes_generated);
		LPT_INFO("cout", "Simulation - Generated nodes with w>2 " << _w_gt2_nodes_generated);
		if (! _config._log_search ) return;

		using namespace rapidjson;

		// Dump optimal_paths and visited into JSON document
		const ProblemInfo& info = ProblemInfo::getInstance();
		Document trace;
		Document::AllocatorType& allocator = trace.GetAllocator();
		trace.SetObject();
		Value domainName;
		domainName.SetString(StringRef(info.getDomainName().c_str()));
		trace.AddMember("domain", domainName.Move(), allocator );
		Value instanceName;
		instanceName.SetString(StringRef(info.getInstanceName().c_str()));
		trace.AddMember("instance", instanceName.Move(), allocator );
		Value visits(kArrayType);
        {
            for ( auto n : _visited ) {
				auto s = n->state;
                Value state(kObjectType);
				JSONArchive::store(state, allocator, s);
                {
					Value v(n->_gen_order);
					state.AddMember( "gen_order", v, allocator);
                }
                visits.PushBack(state.Move(), allocator);
            }
        }
        trace.AddMember("visited", visits, allocator);
		Value opt_paths(kArrayType);
		{
			for ( auto path_to_sub_goal : _optimal_paths ) {
				if ( path_to_sub_goal == nullptr ) continue;
				Value path(kArrayType);
				{
					NodePT node = path_to_sub_goal;

					while (node->has_parent()) {
						Value state(kObjectType);
						JSONArchive::store(state, allocator, node->state);
						path.PushBack( state.Move(),allocator);
						node = node->parent;
					}

					Value s0(kObjectType);
					JSONArchive::store( s0, allocator, node->state );
					path.PushBack( s0.Move(), allocator );
				}
				opt_paths.PushBack(path.Move(),allocator);
			}

		}
		trace.AddMember("optimal_paths", opt_paths, allocator );

		FILE* fp = fopen( "mv_iw_run.json", "wb"); // non-Windows use "w"
		char writeBuffer[65536];
		FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
		Writer<FileWriteStream> writer(os);
		trace.Accept(writer);
		fclose(fp);
	}

protected:

	//! Returns true iff all goal atoms have been reached in the IW search
	bool process_node(NodePT& node) {
		if (_config._complete) return process_node_complete(node);

		const StateT& state = node->state;

		// We iterate through the indexes of all those goal atoms that have not yet been reached in the IW search
		// to check if the current node satisfies any of them - and if it does, we mark it appropriately.
		for (auto it = _unreached.begin(); it != _unreached.end(); ) {
			unsigned subgoal_idx = *it;

			if (_model.goal(state, subgoal_idx)) {
                // MRJ: Reward function
                node->R += 1.0;
// 				node->satisfies_subgoal = true;
// 				_all_paths[subgoal_idx].push_back(node);
				if (!_optimal_paths[subgoal_idx]) _optimal_paths[subgoal_idx] = node;
				it = _unreached.erase(it);
			} else {
				++it;
			}
		}

        update_best_node( node );

		// As soon as all nodes have been processed, we return true so that we can stop the search
		return _unreached.empty();
	}

	//! Returns true iff all goal atoms have been reached in the IW search
	bool process_node_complete(NodePT& node) {
		const StateT& state = node->state;

		for (unsigned i = 0; i < _model.num_subgoals(); ++i) {
			if (!_in_seed[i] && _model.goal(state, i)) {
// 				node->satisfies_subgoal = true;
                node->R += 1.0;
				if (!_optimal_paths[i]) _optimal_paths[i] = node;
				_unreached.erase(i);
			}
		}
        update_best_node( node );
 		return _unreached.empty();
		//return false; // return false so we don't interrupt the processing
	}

    void update_best_node( const NodePT& node ) {
        if ( node->R > _best_node->R )
            _best_node = node;
    }

	void mark_seed_subgoals(const NodePT& node) {
		std::vector<bool> _(_model.num_subgoals(), false);
		_in_seed.swap(_);
		_unreached.clear();
		for (unsigned i = 0; i < _model.num_subgoals(); ++i) {
			if (_model.goal(node->state, i)) {
				_in_seed[i] = true;
			} else {
				_unreached.insert(i);
			}
		}
	}

// public:
// 	const std::unordered_set<NodePT>& get_relevant_nodes() const { return _visited; }
};

} } // namespaces