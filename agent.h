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
#include "board.h"
#include "action.h"
#include <fstream>
#include <chrono>
#include <ctime>

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
class random_player : public agent {
public:
	random_player(const std::string& args = "") : agent(args),
		space(board::size_x * board::size_y), who(board::empty) {
		if (meta.find("seed") != meta.end())
			engine.seed(int(meta["seed"]));
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
			if (move.apply(after) == board::legal)
				return move;
		}
		return action();
	}

protected:
	std::vector<action::place> space;
	board::piece_type who;
	std::default_random_engine engine;
};
class MCTS_player: public random_player{
	class Node{
	public:
		Node(){
			visit = win = 0;
			parent = nullptr;
		};
		int visit;
		int win;
		board b;
		board::piece_type who;
		action::place move;
		struct Node *parent;
		std::vector<struct Node*> child;
	};
public:
	MCTS_player(const std::string& args = "")
		:random_player(args)
	{
		sim_counts = 100;
		time_limit = 1000;
		if(args.find("simulation=") != std::string::npos)
		{
			std::string sims = args.substr(args.find("simulation=") + 11);
			sims = sims.substr(0,sims.find(" "));
			//std::cout<< "simulation=" << sims << std::endl;
			sim_counts = std::stoi(sims);
		}
	}
	virtual action take_action(const board& state) {
		return Simulation(state);
	}
private:
	void deleteSearchTree(Node *root){
		for(Node *child : root->child){
			deleteSearchTree(child);
		}
		delete root;
	}
	Node *selection(Node *root){
		if(root->child.empty())	//leaf
			return root;
		Node *bestNode = nullptr;
		float bestValue = -99999.0f;
		for(Node* current : root->child){
			if(current->visit == 0)
				return current;
			else{
				float c = 0.1;
				float winRate = (float)current->win / current->visit;
				float value = winRate - pow(winRate,2)
					+ c * sqrt(2 * log(root->visit) / current->visit);
				if(value > bestValue){
					bestNode = current;
					bestValue = value;
				}
			}
		}
		return selection(bestNode);
	}
	void expansion(Node *node){
		board::piece_type child_type = (node->who == board::black) ? board::white : board::black;
		std::vector<action::place> child_space(board::size_x * board::size_y);
		for (size_t i = 0; i < child_space.size(); i++)
			child_space[i] = action::place(i, child_type);

		std::shuffle(child_space.begin(), child_space.end(), engine);
		for (const action::place& move : child_space) {
			board after = node->b;
			if (move.apply(after) == board::legal){
				Node *newchild = new Node();
				newchild->parent = node;
				newchild->move = move;
				newchild->b = after;
				newchild->who = child_type;
				node->child.push_back(newchild);
			}
		}
	}
	bool oneSim(Node *node){
		random_player *myself = new random_player("name=white  role=white");
		random_player *opponent = new random_player("name=black  role=black");
		if(this->who == board::black){
			random_player *temp;
			temp = myself;
			myself = opponent;
			opponent = temp;
		}
		bool MYTURN = (node->who == this->who) ? false : true;
		board state = node->b;
		while(true){
			action move;
			if(MYTURN){
				move = myself->take_action(state);
			}else{
				move = opponent->take_action(state);
			}
			if(move.apply(state) != board::legal)
				break;
			MYTURN = !MYTURN;
		}
		delete myself;
		delete opponent;
		return !MYTURN;
	}
	void backPropagation(Node *current, int win){
		if(current == nullptr)
			return;
		current->visit++;
		current->win += win;
		backPropagation(current->parent, win);
	}
	action Simulation(const board& state){
		start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		Node *root = new Node();
		root->b = state;
		root->who = (this->who == board::black) ? board::white : board::black;
		expansion(root);
		if(root->child.empty()){
			delete root;
			return action();
		}
		for(int i = sim_counts; i > 0; ++i){
			Node *current = selection(root);
			if(current->visit > 0){
				expansion(current);
				if(!current->child.empty())
					current = current->child[0];
			}
			int win = oneSim(current) ? 1 : 0;
			backPropagation(current, win);
			cur_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			
			if(cur_time - start_time >= time_limit){
				break;
			}
		}
		float bestWinRate = (float)root->child[0]->win / root->child[0]->visit;
		action::place bestmove = root->child[0]->move;
		for(Node *child : root->child){
			if(child->visit == 0)	continue;
			float winRate = (float)child->win / child->visit;
			if(winRate > bestWinRate){
				bestWinRate = winRate;
				bestmove = child->move;
			}
		}
		deleteSearchTree(root);
		return bestmove;
	}
	//variables
	//std::vector<action::place> space;
	//board::piece_type who;
	int sim_counts;
	time_t time_limit,start_time,cur_time;
};
class player: public random_player{
public:
	player(const std::string& args = "")
		:random_player(args)
	{
		if(args.find("search=") != std::string::npos)
		{
			std::string search = args.substr(args.find("search=") + 7);
			search = search.substr(0,search.find(" "));
			//std::cout<< "search=" << search << std::endl;
			if(search == "MCTS" || search == "mcts"){
				myAgent = new MCTS_player(args);
			}
		}
		else //random
		{	myAgent = new random_player(args);}
	}
	virtual action take_action(const board& state){
		return myAgent->take_action(state);
	}
private:
	agent *myAgent;
};