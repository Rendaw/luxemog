#include "luxemog.h"

#include <sstream>
#include <iostream>
#include <regex>

struct match_map
{
	std::map<std::string, std::shared_ptr<luxem::value>> trees;
	std::map<std::string, std::string> strings;

	void update(match_map &other)
	{
		other.trees.insert(trees.begin(), trees.end());
		other.strings.insert(strings.begin(), strings.end());
	}
};

enum step_result
{
	step_continue,
	step_break,
	step_fail,
	step_push
};

struct scan_context;
struct scan_stackable
{
	virtual ~scan_stackable(void) {}
	virtual step_result step(scan_context &context, step_result last_result) = 0;
};

struct function_scan_stackable : scan_stackable
{
	std::function<step_result(scan_context &context, step_result last_result)> callback;
	step_result step(scan_context &context, step_result last_result) override 
		{ return callback(context, last_result); }
};

struct scan_context
{
	bool verbose;
	bool reverse;
	std::list<luxemog::transform::transform_data *> transform_stack;
	std::list<std::unique_ptr<scan_stackable>> stack;

	std::shared_ptr<luxem::value> &get_from(void) 
		{ return reverse ? transform_stack.back()->to : transform_stack.back()->from; }
	std::shared_ptr<luxem::value> &get_to(void)
		{ return reverse ? transform_stack.back()->from : transform_stack.back()->to; }
};

struct transform_context;
struct transform_stackable
{
	virtual ~transform_stackable(void) {}
	virtual bool step(transform_context &context, match_map &matches) = 0;
};

struct transform_context
{
	bool verbose;
	std::list<std::unique_ptr<transform_stackable>> stack;
};

step_result scan_node(
	scan_context &context,
	match_map &matches,
	std::shared_ptr<luxem::value> &target,
	std::shared_ptr<luxem::value> const &from,
	bool ignore_type = false);

std::shared_ptr<luxem::value> transform_node(
	transform_context &context, 
	match_map const &matches, 
	std::shared_ptr<luxem::value> const &to);

void transform_root(
	match_map &matches, 
	std::shared_ptr<luxem::value> &root,
	std::shared_ptr<luxem::value> const &to,
	bool verbose);

struct build_context;
void build_preprocess(build_context &context, std::shared_ptr<luxem::value> &data);

///////////////////////////////////////////////////////////////////////////////
// special nodes

struct special : luxem::value
{
	static std::string const name;

	virtual ~special(void) {}
	virtual step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) = 0;
	virtual std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) = 0;
};

std::string const special::name("special");

std::string format_string(std::string const &pattern, match_map const &matches)
{
	auto parse = [&pattern](auto callback)
	{
		size_t offset = 0;
		char const *run_start = &pattern[0];
		size_t run_length = 0;
		bool literal = true;
		auto end = [&](void)
		{
			callback(run_start, run_length, literal);
			run_start = &pattern[offset];
			run_length = 0;
		};
		bool escape = false;
		while (offset < pattern.size())
		{
			auto const next = pattern[offset++];
			if (literal && !escape && (next == '<'))
			{
				end();
				literal = false;
			}
			else if (!literal && (next == '>'))
			{
				end();
				literal = true;
			}
			else if (literal && !escape && (next == '%'))
			{
				end();
				escape = true;
			}
			else
			{
				run_length += 1;
				escape = false;
			}
		}
		end();
	};

	size_t expected_length = 0;
	std::list<std::string const *> references;

	parse([&](char const *chunk, size_t chunk_length, bool literal)
	{
		std::cout << "Chunk '" << std::string(chunk, chunk_length) << "', len " << chunk_length << ", literal " << literal << std::endl;
		if (literal) expected_length += chunk_length;
		else
		{
			std::string key(chunk, chunk_length);
			auto found = matches.strings.find(key);
			if (found == matches.strings.end()) 
			{
				std::stringstream message;
				message << "Missing saved value for key '" << key << "'.";
				throw std::runtime_error(message.str());
			}
			expected_length += found->second.size();
			references.push_back(&found->second);
		}
	});

	{
		std::vector<char> out;
		out.resize(expected_length);
		auto ref_iterator = references.begin();
		size_t offset = 0;
		parse([&](char const *chunk, size_t chunk_length, bool literal)
		{
			if (literal) 
			{
				memcpy(&out[offset], chunk, chunk_length);
				offset += chunk_length;
			}
			else
			{
				memcpy(&out[offset], (*ref_iterator)->c_str(), (*ref_iterator)->size());
				offset += (*ref_iterator)->size();
			}
		});
		return std::string(&out[0], out.size());
	}
}

struct build_type : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }

	std::string format;
	std::shared_ptr<luxem::value> value;

	build_type(build_context &context, std::shared_ptr<luxem::value> &&data)
	{ 
		auto &object = data->as<luxem::reader::object_context>();
		object.element("format", [this](std::shared_ptr<luxem::value> &&data) 
			{ format = data->as<luxem::primitive>().get_string(); });
		object.build_struct(
			"value", 
			[this](std::shared_ptr<luxem::value> &&data) 
				{ value = std::move(data); },
			[this, &context](std::string const &, std::shared_ptr<luxem::value> &data)
				{ build_preprocess(context, data); });
	}

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
		{ throw std::runtime_error("*type cannot be used in 'from' patterns."); }
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
	{ 
		auto out = transform_node(context, matches, value);
		out->set_type(format_string(format, matches));
		return out;
	}
};

std::string const build_type::name("*type");

struct build_string : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }

	std::string format;

	build_string(std::shared_ptr<luxem::value> &&data)
		{ format = data->as<luxem::primitive>().get_string(); }

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
		{ throw std::runtime_error("*string cannot be used in 'from' patterns."); }
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ return std::make_shared<luxem::primitive>(format_string(format, matches)); }
};

std::string const build_string::name("*string");

struct regex_definition
{
	bool has_id = false;
	std::string id;
	std::regex regex;
	bool has_replace = false;
	std::string replace;

	regex_definition(std::shared_ptr<luxem::value> &&data)
	{
		if (data->is<luxem::primitive>()) regex = data->as<luxem::primitive>().get_primitive();
		else if (data->is<luxem::reader::object_context>())
		{
			auto &object = data->as<luxem::reader::object_context>();
			auto has_pattern = std::make_shared<bool>(false);
			object.element("id", [this](std::shared_ptr<luxem::value> &&data) 
				{ id = data->as<luxem::primitive>().get_string(); has_id = true; });
			object.element("exp", [this, has_pattern](std::shared_ptr<luxem::value> &&data) 
				{ regex = data->as<luxem::primitive>().get_string(); *has_pattern = true; });
			object.element("replace", [this](std::shared_ptr<luxem::value> &&data) 
				{ replace = data->as<luxem::primitive>().get_string(); has_replace = true; });
			object.finally([this, has_pattern](void)
			{
				if (!*has_pattern) 
					throw std::runtime_error("Regex missing pattern.");
				if (!has_replace && (regex.mark_count() > 1)) 
					throw std::runtime_error("Regex patterns must have at most 1 marked subexpression.");
				if (has_replace && !has_id) 
					throw std::runtime_error("Substitution regexes must have an id.");
			});
		}
		else throw std::runtime_error("A regex pattern must be a primitive or object.");
	}

	bool test(std::string const &source, match_map &matches)
	{
		if (has_id)
		{
			auto found = matches.strings.find(id);
			if (found != matches.strings.end())
			{
				std::stringstream message;
				message << "Match " << id << " stored multiple times.  Matches must only occur once.";
				throw std::runtime_error(message.str());
			}
		}
		std::smatch results;
		if (has_replace)
		{
			matches.strings.emplace(
				id, 
				std::regex_replace(source, regex, replace));
		}
		else
		{
			if (!std::regex_search(source, results, regex)) return false;
			matches.strings.emplace(
				id, 
				regex.mark_count() > 0 ? results.str(1) : results.str(0));
		}
		return true;
	}
};

struct regex_definition_list
{
	std::list<std::unique_ptr<regex_definition>> patterns;

	void deserialize(std::shared_ptr<luxem::value> &&data)
	{
		if (data->is<luxem::primitive>() || data->is<luxem::reader::object_context>())
			patterns.emplace_back(std::make_unique<regex_definition>(std::move(data)));
		else if (data->is<luxem::reader::array_context>())
			data->as<luxem::reader::array_context>().element(
				[this](std::shared_ptr<luxem::value> &&data)
					{ patterns.emplace_back(std::make_unique<regex_definition>(std::move(data))); });
		else assert(false);
	}
	
	bool test(std::string const &source, match_map &matches)
	{
		for (auto &pattern : patterns)
			if (!pattern->test(source, matches)) return false;
		return true;
	}
};

struct regex : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }

	regex_definition_list value_definition;

	regex(std::shared_ptr<luxem::value> &&data) 
		{ value_definition.deserialize(std::move(data)); }

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
	{
		if (!value_definition.test(target->as<luxem::primitive>().get_primitive(), matches)) return step_fail;
		return step_break;
	}
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ throw std::runtime_error("*regex cannot be used in 'to' patterns."); }
};

std::string const regex::name("*regex");

struct type_regex : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }
	
	regex_definition_list type_definition;
	std::shared_ptr<luxem::value> value;

	type_regex(build_context &context, std::shared_ptr<luxem::value> &&data)
	{
		auto &object = data->as<luxem::reader::object_context>();
		object.element(
			"exp",
			[this](std::shared_ptr<luxem::value> &&data)
				{ type_definition.deserialize(std::move(data)); });
		object.build_struct(
			"value",
			[this](std::shared_ptr<luxem::value> &&data)
			{ 
				value = std::move(data); 
				if (value->has_type()) 
					throw std::runtime_error("*type_regex 'value' must not have a type.");
			},
			[this, &context](std::string const &, std::shared_ptr<luxem::value> &data)
				{ build_preprocess(context, data); });
	}

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
	{
		if (!target->has_type()) return step_fail;
		if (!type_definition.test(target->get_type(), matches)) return step_fail;
		return scan_node(context, matches, target, value, true);
	}
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ throw std::runtime_error("*type_regex cannot be used in 'to' patterns."); }
};

std::string const type_regex::name("*type_regex");

struct alternate : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }
	
	std::vector<std::shared_ptr<luxem::value>> patterns;

	alternate(build_context &context, luxem::reader::array_context &array_data)
	{
		array_data.build_struct(
			[this](std::shared_ptr<luxem::value> &&data)
				{ patterns.emplace_back(std::move(data)); },
			[this, &context](std::string const &, std::shared_ptr<luxem::value> &data)
				{ build_preprocess(context, data); });
		array_data.finally([this](void)
		{
			if (patterns.empty()) throw std::runtime_error("Cannot have *alt with no patterns.");
		});
	}

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
	{
		struct stackable : scan_stackable
		{
			match_map &matches;
			match_map branch_matches;
			std::vector<std::shared_ptr<luxem::value>> &patterns;
			std::vector<std::shared_ptr<luxem::value>>::iterator iterator;
			std::shared_ptr<luxem::value> &target;

			stackable(
				match_map &matches,
				std::vector<std::shared_ptr<luxem::value>> &patterns,
				std::shared_ptr<luxem::value> &target) : 
				matches(matches),
				patterns(patterns),
				iterator(patterns.begin()),
				target(target)
				{}

			step_result step(scan_context &context, step_result last_result) override
			{
				if (last_result == step_break) 
				{
					branch_matches.update(matches);
					return step_break;
				}
				if (iterator == patterns.end()) return step_fail;
				branch_matches = matches;
				auto result = scan_node(context, branch_matches, target, *iterator);
				if (result == step_break) return step_break;
				++iterator;
				return step_continue;
			}
		};
		context.stack.emplace_back(std::make_unique<stackable>(matches, patterns, target));
		return step_push;
	}
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ throw std::runtime_error("*alt cannot be used in 'to' patterns."); }
};

std::string const alternate::name("*alt");

struct error : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }

	std::string message;

	error(std::string const &message) : message(message) {}

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
		{ throw std::runtime_error("*error cannot be used in 'from' patterns."); }
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
	{
		if (message.empty()) throw std::runtime_error("Matched forbidden pattern.");
		else throw std::runtime_error(message);
	}
};

std::string const error::name("*error");

struct wildcard : special
{
	static std::string const name;
	std::string const &get_name(void) const override { return name; }

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
	{ 
		return step_break; 
	}
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ throw std::runtime_error("*wild cannot be used in 'to' patterns."); }
};

std::string const wildcard::name("*wild");

struct match_scan_stackable;
struct match_definition
{
	std::string id;
	std::shared_ptr<luxem::value> pattern;

	match_definition(void) : pattern(std::make_shared<wildcard>()) {}

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target)
	{
		if (!pattern) 
		{
			std::stringstream message;
			message << "*match " << id << " is missing 'pattern'.";
			throw std::runtime_error(message.str());
		}

		struct match_scan_stackable : scan_stackable
		{
			match_map &matches;
			std::string const &id;
			std::shared_ptr<luxem::value> &target;
			std::shared_ptr<luxem::value> pattern;

			match_scan_stackable(
				match_map &matches, 
				std::string const &id, 
				std::shared_ptr<luxem::value> &target, 
				std::shared_ptr<luxem::value> pattern) : 
				matches(matches), 
				id(id), 
				target(target), 
				pattern(pattern) 
				{}

			step_result step(scan_context &context, step_result last_result) override
			{
				if (last_result == step_push)
				{
					last_result = scan_node(context, matches, target, pattern);
					if (last_result == step_push) return step_push;
				}
				if (last_result == step_fail) return step_fail;

				auto found = matches.trees.find(id);
				if (found != matches.trees.end())
				{
					std::stringstream message;
					message << "Match " << id << " stored multiple times.  Matches must only occur once.";
					throw std::runtime_error(message.str());
				}
				if (context.verbose) std::cerr << "Saving match " << id << std::endl;
				matches.trees.emplace(id, target);
				return last_result;
			}
		};

		context.stack.emplace_back(std::make_unique<match_scan_stackable>(matches, id, target, pattern));
		return step_push;
	}

	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches)
	{
		auto found = matches.trees.find(id);
		if (found == matches.trees.end())
		{
			std::stringstream message;
			message << "Match " << id << ", required by output, is missing.";
			throw std::runtime_error(message.str());
		}
		return transform_node(context, matches, found->second);
	}
};

struct match_definition_standin : std::shared_ptr<match_definition>, special
{
	using std::shared_ptr<match_definition>::shared_ptr;
	using std::shared_ptr<match_definition>::operator =;

	static std::string const name;
	std::string const &get_name(void) const override { return name; }
	
	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target) override
		{ return (*this)->scan(context, matches, target); }
	
	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches) override
		{ return (*this)->generate(context, matches); }
};
	
std::string const match_definition_standin::name("*match");
	
///////////////////////////////////////////////////////////////////////////////
// scanning

struct scan_root_stackable : scan_stackable
{
	std::shared_ptr<luxem::value> &root;

	struct substackable
	{
		std::function<step_result(scan_context &context, step_result last_result, std::unique_ptr<substackable> &next_state)> callback;
		template <typename callback_type> substackable(callback_type &&callback) : callback(std::move(callback)) {}
		step_result step(scan_context &context, step_result last_result, std::unique_ptr<substackable> &next_state)
			{ return callback(context, last_result, next_state); }
	};

	step_result begin_recurse(std::unique_ptr<substackable> &next_state)
	{
		if (root->is<luxem::object>())
		{
			auto &data = root->as<luxem::object>().get_data();
			auto temp = std::move(next_state);
			next_state = std::make_unique<substackable>(
				[this, &data, iterator = data.begin()](
					scan_context &context, 
					step_result last_result, 
					std::unique_ptr<substackable> &next_state) mutable
				{
					if (iterator == data.end()) return step_break;
					context.stack.push_back(std::make_unique<scan_root_stackable>(iterator->second));
					++iterator;
					return step_push;
				});
		}
		else if (root->is<luxem::array>())
		{
			auto &data = root->as<luxem::array>().get_data();
			next_state = std::make_unique<substackable>(
				[this, &data, iterator = data.begin()](
					scan_context &context, 
					step_result last_result, 
					std::unique_ptr<substackable> &next_state) mutable
				{
					if (iterator == data.end()) return step_break;
					context.stack.push_back(std::make_unique<scan_root_stackable>(*iterator));
					++iterator;
					return step_push;
				});
		}
		else return step_break;
		return step_continue;
	}
	
	step_result begin_subtransform(scan_context &context, std::unique_ptr<substackable> &next_state)
	{
		auto temp = std::move(next_state);
		next_state = std::make_unique<substackable>(
			[this, iterator = context.transform_stack.back()->subtransforms.begin()](
				scan_context &context, 
				step_result last_result, 
				std::unique_ptr<substackable> &next_state) mutable
			{
				if (last_result != step_continue)
					context.transform_stack.pop_back();
				if (iterator == context.transform_stack.back()->subtransforms.end()) 
				{
					return begin_recurse(next_state);
				}
				context.transform_stack.push_back(iterator->get());
				context.stack.push_back(std::make_unique<scan_root_stackable>(root));
				++iterator;
				return step_push;
			});
		return step_continue;
	}

	std::unique_ptr<substackable> state;

	scan_root_stackable(std::shared_ptr<luxem::value> &root) : root(root) 
	{
		state = std::make_unique<substackable>(
			[this, matches = match_map()](
				scan_context &context, 
				step_result last_result, 
				std::unique_ptr<substackable> &next_state) mutable
			{
				// Start by scanning root
				if (last_result == step_push)
				{
					if (context.verbose) 
						std::cerr << "Scanning " << this->root->get_name() << std::endl;
					if (!context.get_from()) 
						throw std::runtime_error("Transform missing 'from' pattern.");
					last_result = scan_node(context, matches, this->root, context.get_from());
					if (last_result == step_push) return step_push;
				}

				// Scan finished, transform if successful
				if (last_result == step_fail) 
				{
					if (context.verbose) 
						std::cerr << "Failed to match " << this->root->get_name() << std::endl;
				}
				else
				{
					if (context.verbose) 
						std::cerr << "Matched " << this->root->get_name() << std::endl;
					if (context.get_to())
						transform_root(matches, this->root, context.get_to(), context.verbose);
					return begin_subtransform(context, next_state);
					return step_continue;
				}

				return begin_recurse(next_state);
			});
	}

	step_result step(scan_context &context, step_result last_result) override
		{ return state->callback(context, last_result, state); }
};

struct object_scan_stackable : scan_stackable
{
	match_map &matches;
	luxem::object &target, &from;

	luxem::object::object_data::iterator iterator;

	object_scan_stackable(match_map &matches, luxem::object &target, luxem::object &from) :
		matches(matches),
		target(target),
		from(from),
		iterator(from.get_data().begin())
		{}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (last_result == step_fail) return step_fail;
		if (iterator == from.get_data().end()) return step_break;
		auto target_found = target.get_data().find(iterator->first);
		if (target_found == target.get_data().end()) return step_fail;
		auto result = scan_node(context, matches, target_found->second, iterator->second);
		if (result == step_fail) return step_fail;
		++iterator;
		return result;
	}
};

struct array_scan_stackable : scan_stackable
{
	match_map &matches;
	luxem::array &target, &from;

	luxem::array::array_data::iterator target_iterator, from_iterator;

	array_scan_stackable(match_map &matches, luxem::array &target, luxem::array &from) :
		matches(matches),
		target(target),
		from(from),
		target_iterator(target.get_data().begin()),
		from_iterator(from.get_data().begin())
		{}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (last_result == step_fail) return step_fail;
		if (from_iterator == from.get_data().end()) return step_break;
		auto result = scan_node(context, matches, *target_iterator, *from_iterator);
		if (result == step_fail) return step_fail;
		++target_iterator;
		++from_iterator;
		return result;
	}
};

step_result scan_node(
	scan_context &context, 
	match_map &matches, 
	std::shared_ptr<luxem::value> &target,
	std::shared_ptr<luxem::value> const &from,
	bool ignore_type)
{
	if (context.verbose) std::cerr << "Comparing " << target->get_name() << " to " << from->get_name() << std::endl;

	auto check_type = [&](void)
	{
		if (ignore_type) return true;
		if (target->has_type() != from->has_type()) return false;
		if (from->has_type() && (target->get_type() != from->get_type())) return false;
		return true;
	};

	if (from->is<luxem::primitive>())
	{
		if (!check_type()) return step_fail;
		if (!target->is<luxem::primitive>()) return step_fail;
		auto &from_resolved = from->as<luxem::primitive>();
		auto &target_resolved = target->as<luxem::primitive>();
		if (target_resolved.get_primitive() != from_resolved.get_primitive()) return step_fail;
		return step_break;
	}
	else if (from->is<luxem::object>())
	{
		if (!check_type()) return step_fail;
		if (!target->is<luxem::object>()) return step_fail;
		auto &from_resolved = from->as<luxem::object>();
		auto &target_resolved = target->as<luxem::object>();
		if (from_resolved.get_data().size() != target_resolved.get_data().size()) return step_fail;
		context.stack.push_back(std::make_unique<object_scan_stackable>(
			matches, 
			target_resolved,
			from_resolved));
		return step_push;
	}
	else if (from->is<luxem::array>())
	{
		if (!check_type()) return step_fail;
		if (!target->is<luxem::array>()) return step_fail;
		auto &from_resolved = from->as<luxem::array>();
		auto &target_resolved = target->as<luxem::array>();
		if (from_resolved.get_data().size() != target_resolved.get_data().size()) return step_fail;
		context.stack.push_back(std::make_unique<array_scan_stackable>(
			matches, 
			target_resolved,
			from_resolved));
		return step_push;
	}
	else if (from->is_derived<special>())
	{
		return from->as_derived<special>().scan(context, matches, target);
	}
	else
	{
		assert(false);
		return step_fail;
	}
}

///////////////////////////////////////////////////////////////////////////////
// transforming

struct object_transform_stackable : transform_stackable
{
	std::shared_ptr<luxem::object> out;
	luxem::object const &to;

	luxem::object::object_data::const_iterator to_iterator;

	object_transform_stackable(std::shared_ptr<luxem::object> out, luxem::object &to) : 
		out(out), 
		to(to), 
		to_iterator(to.get_data().begin()) 
		{}

	bool step(transform_context &context, match_map &matches) override
	{
		if (to_iterator == to.get_data().end()) return false;
		out->get_data().emplace(to_iterator->first, transform_node(context, matches, to_iterator->second));
		++to_iterator;
		return true;
	}
};

struct array_transform_stackable : transform_stackable
{
	std::shared_ptr<luxem::array> out;
	luxem::array const &to;

	luxem::array::array_data::const_iterator to_iterator;

	array_transform_stackable(std::shared_ptr<luxem::array> out, luxem::array &to) : 
		out(out), 
		to(to), 
		to_iterator(to.get_data().begin()) 
		{}

	bool step(transform_context &context, match_map &matches) override
	{
		if (to_iterator == to.get_data().end()) return false;
		out->get_data().emplace_back(transform_node(context, matches, *to_iterator));
		++to_iterator;
		return true;
	}
};

std::shared_ptr<luxem::value> transform_node(
	transform_context &context, 
	match_map const &matches, 
	std::shared_ptr<luxem::value> const &to)
{
	if (to->is<luxem::primitive>())
	{
		auto out = std::make_shared<luxem::primitive>(to->as<luxem::primitive>().get_primitive());
		if (to->has_type()) out->set_type(to->get_type());
		return out;
	}
	else if (to->is<luxem::object>())
	{
		auto out = std::make_shared<luxem::object>();
		if (to->has_type()) out->set_type(to->get_type());
		context.stack.emplace_back(std::make_unique<object_transform_stackable>(out, to->as<luxem::object>()));
		return out;
	}
	else if (to->is<luxem::array>())
	{
		auto out = std::make_shared<luxem::array>();
		if (to->has_type()) out->set_type(to->get_type());
		context.stack.emplace_back(std::make_unique<array_transform_stackable>(out, to->as<luxem::array>()));
		return out;
	}
	else if (to->is_derived<special>())
	{
		return to->as_derived<special>().generate(context, matches);
	}
	else
	{
		assert(false);
		return {};
	}
}

void transform_root(
	match_map &matches, 
	std::shared_ptr<luxem::value> &root,
	std::shared_ptr<luxem::value> const &to,
	bool verbose)
{
	transform_context context{verbose};
	root = transform_node(context, matches, to);
	while (!context.stack.empty()) 
		if (!context.stack.back()->step(context, matches))
			context.stack.pop_back();
}

///////////////////////////////////////////////////////////////////////////////
// rule deserialization 

struct build_context
{
	bool root;
	std::map<std::string, std::shared_ptr<match_definition>> match_definitions;
		
	struct pre_match_definition
	{
		std::string id;
		std::shared_ptr<luxem::value> pattern;
	};

	std::shared_ptr<match_definition> get_definition(std::string const &id)
	{
		auto found = match_definitions.find(id);
		if (found != match_definitions.end())
		{
			return found->second;
		}
		else
		{
			auto definition = std::make_shared<match_definition>();
			definition->id = id;
			match_definitions.emplace(id, definition);
			return definition;
		}
	}

	std::shared_ptr<luxem::value> match_from_object(std::shared_ptr<luxem::value> &data);
};

bool build_special(build_context &context, std::shared_ptr<luxem::value> &data)
{
	if (data->get_type() == "*match")
	{
		if (data->is<luxem::primitive>())
		{
			auto &name = data->as<luxem::primitive>().get_primitive();
			data = std::make_shared<match_definition_standin>(context.get_definition(name));
		}
		else if (data->is<luxem::reader::object_context>())
		{
			data = context.match_from_object(data);
		}
		else throw std::runtime_error("*match definitions must be strings or objects.");
	}
	else if (data->get_type() == "*wild")
	{
		auto replacement = std::make_shared<wildcard>();
		/*data->as<luxem::reader::object_context>().element(
			"recurse", 
			[replacement](std::shared_ptr<luxem::value> &&data)
			{ 
				replacement->recurse = data->as<luxem::primitive>().get_bool(); 
				std::cout << "raw is " << data->as<luxem::primitive>().get_primitive() << std::endl;
				std::cout << "raw translated is " << data->as<luxem::primitive>().get_bool() << std::endl;
				std::cout << "recurse is " << replacement->recurse << std::endl;
			});
		*/
		data = replacement;
	}
	else if (data->get_type() == "*alt")
	{
		auto &array_data = data->as<luxem::reader::array_context>();
		data = std::make_shared<alternate>(context, array_data);
	}
	else if (data->get_type() == "*regex")
	{
		data = std::make_shared<regex>(std::move(data));
	}
	else if (data->get_type() == "*type_regex")
	{
		data = std::make_shared<type_regex>(context, std::move(data));
	}
	else if (data->get_type() == "*string")
	{
		data = std::make_shared<build_string>(std::move(data));
	}
	else if (data->get_type() == "*type")
	{
		data = std::make_shared<build_type>(context, std::move(data));
	}
	else if (data->get_type() == "*error")
	{
		data = std::make_shared<error>(data->as<luxem::primitive>().get_string());
	}
	else return false;
	return true;
}

void build_preprocess(build_context &context, std::shared_ptr<luxem::value> &data)
{
	if (data->has_type())
	{
		if (build_special(context, data)) {}
		else if (data->get_type().substr(0, 1) == "*")
		{
			data->set_type(data->get_type().substr(1));
		}
	}
}
	
std::shared_ptr<luxem::value> build_context::match_from_object(std::shared_ptr<luxem::value> &data)
{
	auto &object_data = data->as<luxem::reader::object_context>();
	auto standin = std::make_shared<match_definition_standin>();
	auto match_context = std::make_shared<pre_match_definition>();
	object_data.element("id", [match_context](std::shared_ptr<luxem::value> &&data)
	{
		match_context->id = data->as<luxem::primitive>().get_primitive();
	});
	object_data.build_struct(
		"pattern", 
		[match_context](std::shared_ptr<luxem::value> &&data)
		{
			match_context->pattern = std::move(data);
		},
		[this](std::string const &, std::shared_ptr<luxem::value> &data)
		{ 
			build_preprocess(*this, data); 
		});
	object_data.finally([&, match_context, standin](void)
	{
		if (match_context->id.empty()) throw std::runtime_error("Missing *match name.");
		*standin = get_definition(match_context->id);
		if (match_context->pattern)
			(*standin)->pattern = std::move(match_context->pattern);
	});
	
	return standin;
}

///////////////////////////////////////////////////////////////////////////////
// interface

namespace luxemog
{

transform::transform(std::shared_ptr<luxem::value> &&data, bool verbose) : 
	verbose(verbose), data(std::move(data))
{
}

transform::transform_data::transform_data(std::shared_ptr<luxem::value> &&data)
{
	auto &object = data->as<luxem::reader::object_context>();
	
	{
		auto context = std::make_shared<build_context>();

		object.build_struct(
			"from", 
			[this](std::shared_ptr<luxem::value> &&data) { from = std::move(data); }, 
			[context](std::string const &, std::shared_ptr<luxem::value> &data)
			{ 
				build_preprocess(*context, data); 
			}
		);

		object.build_struct(
			"to", 
			[this](std::shared_ptr<luxem::value> &&data) { to = std::move(data); }, 
			[context](std::string const &, std::shared_ptr<luxem::value> &data)
			{ 
				build_preprocess(*context, data); 
			}
		);

		object.element("matches", [this, context](std::shared_ptr<luxem::value> &&data)
		{
			data->as<luxem::reader::array_context>().element([this, context](std::shared_ptr<luxem::value> &&data)
			{
				if (!build_special(*context, data)) 
					throw std::runtime_error("Only specials may be defined in 'matches'.");
			});
		});
	}

	object.element(
		"subtransforms",
		[this](std::shared_ptr<luxem::value> &&data) 
		{
			data->as<luxem::reader::array_context>().element([this](std::shared_ptr<luxem::value> &&data)
				{ subtransforms.emplace_back(std::make_unique<transform_data>(std::move(data))); });
		}
	);
}

void transform::apply(std::shared_ptr<luxem::value> &target, bool reverse)
{
	scan_context context{verbose, reverse};
	context.transform_stack.push_back(&data);
	context.stack.push_back(std::make_unique<scan_root_stackable>(target));

	size_t count = 0; // DEBUG
	step_result last_result = step_push;
	while (!context.stack.empty())
	{
		++count; // DEBUG
		if (count > 1000) assert(false); // DEBUG
		last_result = context.stack.back()->step(context, last_result);
		switch (last_result)
		{
			case step_fail: context.stack.pop_back(); break;
			case step_break: context.stack.pop_back(); break;
			default: break;
		}
	}
}
	
transform_list::transform_list(bool verbose) : verbose(verbose) {}

void transform_list::deserialize(std::shared_ptr<luxem::value> &&root)
{
	root->as<luxem::reader::array_context>().element([this](std::shared_ptr<luxem::value> &&data)
		{ transforms.emplace_back(std::make_unique<transform>(std::move(data), verbose)); });
}

void transform_list::apply(std::shared_ptr<luxem::value> &target, bool reverse)
{
	for (auto &transform : transforms) transform->apply(target, reverse);
}

}

