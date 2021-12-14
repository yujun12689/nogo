/**
 * Framework for NoGo and similar games (C++ 11)
 * agent.h: Define the behavior of variants of the player
 *
 * Author: Theory of Computer Games (TCG 2021)
 *         Computer Games and Intelligence (CGI) Lab, NYCU, Taiwan
 *         https://cgilab.nctu.edu.tw/
 */

#pragma once
#include <string>
#include <random>
#include <sstream>
#include <map>
#include <type_traits>
#include <algorithm>
#include <chrono>
#include "board.h"
#include "action.h"
#include <fstream>
#include <set>


struct empty_pos
{
	/* data */
	board::point pos;
	bool use = false;
};

class agent {
public:
	agent(const std::string& args = "") {
		std::stringstream ss("name=unknown role=unknown " + args);
		for (std::string pair; ss >> pair; ) {
			std::string key = pair.substr(0, pair.find('='));
			std::string value = pair.substr(pair.find('=') + 1);
			meta[key] = { value };
		}
	}
	virtual ~agent() {}
	virtual void open_episode(const std::string& flag = "") {}
	virtual void close_episode(const std::string& flag = "") {}
	virtual action take_action(const board& b) { return action(); }
	virtual bool check_for_win(const board& b) { return false; }

public:
	virtual std::string property(const std::string& key) const { return meta.at(key); }
	virtual void notify(const std::string& msg) { meta[msg.substr(0, msg.find('='))] = { msg.substr(msg.find('=') + 1) }; }
	virtual std::string name() const { return property("name"); }
	virtual std::string role() const { return property("role"); }
	//select search
	virtual std::string search() const { return property("search"); }
	//simulation times
	virtual std::string simulation() const { return property("simulation"); }

protected:
	typedef std::string key;
	struct value {
		std::string value;
		operator std::string() const { return value; }
		template<typename numeric, typename = typename std::enable_if<std::is_arithmetic<numeric>::value, numeric>::type>
		operator numeric() const { return numeric(std::stod(value)); }
	};
	std::map<key, value> meta;
};

/**
 * base agent for agents with randomness
 */
class random_agent : public agent {
public:
	random_agent(const std::string& args = "") : agent(args) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
	}
	virtual ~random_agent() {}

protected:
	std::default_random_engine engine;
};

/**
 * random player for both side
 * put a legal piece randomly
 */
class player : public random_agent {
public:
	player(const std::string& args = "") : random_agent("name=random role=unknown " + args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	virtual action take_action(const board& state) {
		std::shuffle(space.begin(), space.end(), engine);
		
		for (const action::place& move : space) {
			board after = state;
			int tmp = move.position().i;
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
};

class Node {
public:
	const void init_root() noexcept {
		root_ = true;
		parent_ = nullptr;
	}

	const void init_bw(board::piece_type bw) noexcept {
		bw_ = bw;
	}
	const bool is_root_or_not() noexcept { return root_;}
   	constexpr Node *get_parent() const noexcept { return parent_; };
   	constexpr bool has_children() const noexcept { return children_size_ > 0; }
	int select_child(board::piece_type &bw, board::point &pos) {
   		float max_score = -10000.f;
		int max_i = -1;
   		for (size_t i = 0; i < children_size_; ++i) {
			//&很重要，不然就要用children_[i]
       		auto &child = children_[i];
       		const float score = (child.rave_wins_ + child.wins_ +
                        std::sqrt(log_visits_ * child.visits_) * 0.25f) /
                        (child.rave_visits_ + child.visits_);
       		child.uct_score_ = score;
       		//max_score = (score - max_score > 0.0001f) ? score : max_score;
			if(score - max_score > 0.0001f){
				max_score = score;
				max_i = i;
				bw = child.bw_;
				//pos = child.pos_;
				pos = board::point(child.pos_);
			}
   		}
   		return max_i;
   	}

	bool expand(const board &b,std::vector<empty_pos> emp_pos_vec, int emp_pos_count) noexcept {
		if(visits_ == 0 || is_leaf_) return false;

		board::piece_type child_bw;
		child_bw = (bw_ == board::black)?board::white:board::black;
		// make_unique is invalid in C++ 11, and tcglinux is C++ 11, not C++ 14!!!
		//children_ = std::make_unique<Node[]>(emp_pos_count);
		children_ = new Node[emp_pos_count];
		//for (const action::place& move : space) {
		for (const empty_pos &move : emp_pos_vec) {
			//if(b[move.position().x][move.position().y] != 0){
			//	continue;
			//}
			if(move.pos.x == -1 && move.pos.y == -1){
				printf("WTF is this\n");
			}
			if(move.use == true) continue;
			board after = b;
			if(after.place(move.pos, child_bw) == board::legal){
				//printf("tmp_pos: %d,%d,%d\n", move.pos.x, move.pos.y, move.pos.i);
				children_[children_size_].init(child_bw, move.pos, this);
				children_size_ ++;
			}
			//if (move.apply(after) == board::legal){
			//	children_[children_size_].init(child_bw, move.position(), this);
			//	children_size_ ++;
			//}
		}
		if(children_size_ == 0){
			is_leaf_ = true;
			return false;
		}

   		return true;
   	}
	void update(size_t winner,
            const std::set<int> &bpos, const std::set<int> &wpos) noexcept {
   		++visits_;
   		log_visits_ = std::log(visits_);

        size_t cwin;
		std::set<int> tmp_pos;
		if (static_cast<size_t>(winner == bw_)){
			//一般的monte carlo tree search
			wins_ += 1;
			cwin = 0;
		}
		else{
			cwin = 1;
		}
		//child的bw_是另一方的
		if(bw_ == board::black){
			tmp_pos = wpos;
		}
		else{
			tmp_pos = bpos;
		}

		//rave
		//printf("children_size_ : %d\n", children_size_);
   		for (size_t i = 0; i < children_size_; ++i) {
       		auto &child = children_[i];
			std::set<int>::iterator it;
			//printf("child_pos_i: %d\n", child.pos_.i);
			it = std::find(tmp_pos.begin(), tmp_pos.end(), child.pos_.i);
			if(it != tmp_pos.end()){
				++child.rave_visits_;
       			child.rave_wins_ += cwin;
			}
			/*
			if(std::find(tmp_pos.begin(), tmp_pos.end(), child.pos_.i) != tmp_pos.end()){
				++child.rave_visits_;
       			child.rave_wins_ += cwin;
			}
			*/
   		}
   	}
/*
   	void get_children_visits(std::unordered_map<board::point, size_t> &visits) const
       	noexcept {
   		for (size_t i = 0; i < children_size_; ++i) {
       		const auto &child = children_[i];
       		if (child.visits_ > 0) {
       			visits.emplace(child.pos_, child.visits_);
       		}
   		}
   	}
*/

	int get_best_move(){
		//int max_i = -1;
		int max_i = 0;
		int max_visits = 0;
		int max_wins = 0;
		board::point best_move;
		
		for (size_t i = 0; i < children_size_; ++i) {
       		const auto &child = children_[i];
			if(child.pos_.x == -1 && child.pos_.y == -1){
				printf("WTF is this\n");
			}
			if (child.visits_ == 0) continue;
			if(child.valid_ == false) continue;
       		if (child.visits_ >= max_visits) {
       			if(child.wins_ > max_wins) 
				   //printf("what is this: %d, %d\n", child.pos_.x, child.pos_.y);
				   max_visits = child.visits_;
				   max_wins = child.wins_;
				   max_i = i;
				   //best_move.x = child.pos_.x;
				   //best_move.y = child.pos_.y;
				   //best_move.i = best_move.x + best_move.y*9;
				   //best_move.i = child.pos_.i;
				   best_move = board::point(child.pos_);
       		}
   		}

		//return &children_[max_i];
		return max_i;
		/*
		//const auto &child = children_[max_i];
		//return children_[max_i].pos_;
		if(best_move.x == -1 && best_move.y == -1){
				printf("WTF is this\n");
				printf("child_size : %d, max_i: %d\n", children_size_, max_i);
		}
		return best_move;
		*/
	}
// private:
	inline const void init(board::piece_type who, board::point pos, Node *parent) noexcept {
		bw_ = who;
		pos_ = pos;
		//pos_.x = pos.x;
		//pos_.y = pos.y;
		//pos_.i = pos.i;
		pos_ = board::point(pos);
   		parent_ = parent;
		valid_ = true;
		/*
		children_size_ = 0;
		wins_ = 0;
		visits_ = 0;
		rave_wins_ = 10;
		rave_visits_ = 20;
		log_visits_ = 0.f;
		*/
   	}
   	size_t children_size_ = 0;
	// make_unique is invalid in C++ 11, and tcglinux is C++ 11, not C++ 14!!!
	// So use *children_ instead!!!
   	//std::unique_ptr<Node[]> children_;
	Node *children_;
	board::point pos_;
	board::piece_type bw_;
	bool root_ = false;
   	bool is_leaf_ = false;
	bool valid_ = false;
   	Node *parent_ = nullptr;
   	size_t wins_ = 0, visits_ = 0, rave_wins_ = 10, rave_visits_ = 20;
   	float log_visits_ = 0.f, uct_score_;
};


class MCTSAgent : public random_agent {
public:
	using hclock = std::chrono::high_resolution_clock;
	//const static constexpr auto threshold_time = std::chrono::seconds(1);
	//const static int threshold_time = 1;

	MCTSAgent(const std::string& args = "") : random_agent("name=MCTSAgent role=unknown " + args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (name().find_first_of("[]():; ") != std::string::npos)
			throw std::invalid_argument("invalid name: " + name());
		if (role() == "black") who = board::black;
		if (role() == "white") who = board::white;
		if (who == board::empty)
			throw std::invalid_argument("invalid role: " + role());
		// search method
		if(args.find("search") != std::string::npos)
			if (search() == "MCTS" ) activate_MCTS = true;
			else if (search() == "random") activate_MCTS = false;
		// simulation counts
		if(args.find("simulation") != std::string::npos){
			if (std::stoi(simulation()) > 0)
				simulation_count = std::stoi(simulation());
			else
				throw std::invalid_argument("invalid simulation: " + simulation());
		}
		for (size_t i = 0; i < space.size(); i++)
			space[i] = action::place(i, who);
	}

	void deleteNode(Node *root){
		if(root != nullptr){
			for(size_t i = 0; i < root->children_size_; i++){
				deleteNode(&root->children_[i]);
			}
			if(root->children_size_ == 0) return;
			delete[] root->children_;
		}
	}

	virtual void close_episode(const std::string& flag = "") {
		//delete whole tree
		deleteNode(init_root);
		delete init_root;
		first_time = true;
		emp_pos_vec.clear();
		std::vector <empty_pos>().swap(emp_pos_vec);    //清除容器并最小化它的容量，
		// vecInt.swap(vector<int>()) ; //另一种写法
		int j= emp_pos_vec.capacity();           //j=0
		int i = emp_pos_vec.size();              //i=0
		emp_pos_vec_size = 0;
		emp_pos_count = 0;
		if(root != nullptr)
			root = nullptr;
	}

	virtual action take_action(const board& state) {

		if(activate_MCTS == false){
			//test
			std::shuffle(space.begin(), space.end(), engine);
			for (const action::place& move : space) {
				board after = state;
				if (move.apply(after) == board::legal)
					return move;
			}
			return action();
		}

		//printf("take turn!!!\n");

		size_t total_counts = 0;
		const auto start_time = hclock::now();
		if(first_time){
			first_time = false;
			last_board = state;
			layout = last_board.get_stone();
			last_layout = layout;
			root = new Node();
			root->init_root();
			init_root = root;
			
			if(who == board::black){
				root->init_bw(board::white);
			}
			else{
				root->init_bw(board::black);
			}
			
			//root->init_bw(who);

			empty_pos tmp_pos;
			for(int x = 0; x < board::size_x; x++){
				for(int y = 0; y < board::size_y; y++){
					if(layout[x][y] == board::empty){
						//tmp_pos.pos.x = x;
						//tmp_pos.pos.y = y;
						//tmp_pos.pos.i = x + y*9;
						tmp_pos.pos = board::point(x,y);
						//printf("tmp_pos: %d,%d,%d\n", tmp_pos.pos.x, tmp_pos.pos.y, tmp_pos.pos.i);
						emp_pos_vec.push_back(tmp_pos);
						emp_pos_count++;
					}
				}
			}
			emp_pos_vec_size = emp_pos_count;
		}
		else{
			board::point tmp_pos;
			bool find_pos = false;
			last_layout = last_board.get_stone();
			last_board = state;
			layout = last_board.get_stone();
			for(int x = 0; x < board::size_x; x++){
				for(int y = 0; y < board::size_y; y++){
					if(layout[x][y] != last_layout[x][y]){
						//tmp_pos.x = x;
						//tmp_pos.y = y;
						tmp_pos = board::point(x,y);
						//printf("tmp_pos: %d,%d,%d\n", tmp_pos.x, tmp_pos.y, tmp_pos.i);
						find_pos = true;
						break;
					}
				}
				if(find_pos == true) break;
			}

			bool find_child = false;
			for(int i = 0; i < root->children_size_; i++){
				if(root->children_[i].pos_.x == tmp_pos.x && root->children_[i].pos_.y == tmp_pos.y){
					root = &root->children_[i];
					find_child = true;
					break;
				}
			}
			if(find_child == false){
				delete root;
				root = new Node();
				if(who == board::black){
					root->init_bw(board::white);
				}
				else{
					root->init_bw(board::black);
				}
			}
			//新的root，上面的就不理了
			root->init_root();
		}
		// Node root;
		do{
			Node *node = root;
			board after = state;
			//black move, for rave use
			//std::vector<int> bpos;
			std::set<int> bpos;
			//white move,for rave use
			//std::vector<int> wpos;
			std::set<int> wpos;
			board::piece_type bw;
			board::point pos;
			while(node->has_children()){
				//printf("has children\n");
				node = &node->children_[node->select_child(bw, pos)];
				after.place(pos, bw);
				if(bw == board::black){
					//bpos.push_back(pos.i);
					bpos.insert(pos.i);
				}
				else{
					//wpos.push_back(pos.i);
					wpos.insert(pos.i);
				}
			}
			if(node->expand(after, emp_pos_vec, emp_pos_count)){
				//printf("%d\n", node->children_size_);
				node = &node->children_[node->select_child(bw, pos)];
				//printf("%d\n", node->children_size_);
				after.place(pos, bw);
				if(bw == board::black){
					//bpos.push_back(pos.i);
					bpos.insert(pos.i);
				}
				else{
					//wpos.push_back(pos.i);
					wpos.insert(pos.i);
				}
			}

			//printf("expand end\n");

			//simulate
			board::piece_type winner = board::empty;
			board::piece_type take_turn = (node->bw_ == board::black)?board::white:board::black;
			//std::vector<empty_pos> tmp_vec;
			//tmp_vec.assign(emp_pos_vec.begin(), emp_pos_vec.end());
			//std::shuffle(tmp_vec.begin(), tmp_vec.end(), engine);
			//std::shuffle(emp_pos_vec.begin(), emp_pos_vec.end(), engine);
			bool has_move = false;
			while(1){
				std::vector<empty_pos> tmp_vec;
				tmp_vec.assign(emp_pos_vec.begin(), emp_pos_vec.end());
				std::shuffle(tmp_vec.begin(), tmp_vec.end(), engine);
				//printf("stuck in this?!\n");
				for (empty_pos &move : tmp_vec){
					if(move.use != true){
						if(after.place(move.pos, take_turn) == board::legal){
							move.use = true;
							has_move = true;
							if(take_turn == board::black){
								//bpos.push_back(move.pos.i);
								bpos.insert(move.pos.i);
							}
							else{
								//wpos.push_back(move.pos.i);
								wpos.insert(move.pos.i);
							}
							break;
						}
					}
				}
				if(has_move){
					take_turn = (take_turn == board::black)?board::white:board::black;
					has_move = false;
				}
				else{
					winner = (take_turn == board::black)?board::white:board::black;
					break;
				}
			}

			/*
			do
			{
				printf("update \n");
				node->update(winner, bpos, wpos);
			} while (node->root_ != true);
			*/

			while(node != nullptr){
				node->update(winner, bpos, wpos);
				node = node->get_parent();
			}

			//printf("%d\n", total_counts);

		}while(++total_counts < simulation_count &&
             (hclock::now() - start_time) < std::chrono::seconds(1));

		//}while(++total_counts < simulation_count);

		const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                              hclock::now() - start_time)
                              .count();
		/*
    	std::cerr << duration << " ms" << std::endl
              << total_counts << " simulations" << std::endl;
		*/

		//print while testing
		//std::cerr << duration << " ms,\t" << total_counts << " simulations" << std::endl;

		/*
		if(root->is_leaf_){
			return action();
		}
		*/
		/*
		std::unordered_map<board::point, size_t> visits;
    	root->get_children_visits(visits);
    	board::point best_move = std::max_element(std::begin(visits), std::end(visits),
                                        [](const auto &p1, const auto &p2) {
                                          return p1.second < p2.second;
                                        })
                           ->first;
		*/
		//return action();
		//board::point best_move = root->get_best_move();

		int max = root->get_best_move();
		root = &root->children_[max];
		int tmp = last_board.place(root->pos_, who);
		if(tmp == board::legal)
			return action::place(root->pos_, who);

		return action();
	}

private:
	std::vector<action::place> space;
	board::piece_type who;
	bool activate_MCTS = true;
	board last_board;
	Node *root = nullptr;
	Node *init_root = nullptr;
	bool first_time = true;
	board::grid last_layout;
	board::grid layout;
	
	std::vector<empty_pos> emp_pos_vec;
	int emp_pos_vec_size = 0;
	int emp_pos_count = 0;

	int simulation_count = 50000;
};