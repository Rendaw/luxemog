#include "luxemog.h"

#include <sstream>
#include <iostream>

typedef std::map<std::string, std::shared_ptr<luxem::value>> match_map;

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

struct scan_context
{
	bool verbose;
	bool reverse;
	std::shared_ptr<luxem::value> &from, &to;
	std::list<std::unique_ptr<scan_stackable>> stack;
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
	bool is_root,
	std::shared_ptr<luxem::value> &target,
	std::shared_ptr<luxem::value> const &from);

std::shared_ptr<luxem::value> transform_node(
	transform_context &context, 
	match_map const &matches, 
	std::shared_ptr<luxem::value> const &to);

void transform_root(
	match_map &matches, 
	std::shared_ptr<luxem::value> &root,
	std::shared_ptr<luxem::value> const &to,
	bool verbose);

struct match_scan_stackable;
struct match_definition
{
	std::string id;

	step_result scan(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> &target)
	{
		context.stack.emplace_back(std::make_unique<match_scan_stackable>(matches, id, target));
		return step_push;
	}

	std::shared_ptr<luxem::value> generate(transform_context &context, match_map const &matches)
	{
		auto found = matches.find(id);
		if (found == matches.end())
		{
			std::stringstream message;
			message << "Match " << id << ", required by output, is missing.";
			throw std::runtime_error(message.str());
		}
		return transform_node(context, matches, found->second);
	}
};

struct root_scan_stackable;
struct match_scan_stackable : scan_stackable
{
	match_map &matches;
	std::string const &id;
	std::shared_ptr<luxem::value> &target;

	match_scan_stackable(match_map &matches, std::string const &id, std::shared_ptr<luxem::value> &target) : matches(matches), id(id), target(target) {}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (last_result == step_push)
		{
			context.stack.emplace_back(std::make_unique<root_scan_stackable>(target));
			return step_push;
		}

		auto found = matches.find(id);
		if (found != matches.end())
		{
			std::stringstream message;
			message << "Match " << id << " matched multiple times.  Matches must only occur once.";
			throw std::runtime_error(message.str());
		}
		if (context.verbose) std::cerr << "Saving match " << id << std::endl;
		matches.emplace(id, target);
		return last_result;
	}
};

struct match_definition_standin : std::shared_ptr<match_definition>, luxem::value
{
	using std::shared_ptr<match_definition>::shared_ptr;
	using std::shared_ptr<match_definition>::operator =;

	static std::string const name;
	std::string const &get_name(void) const override { return name; }
};
	
std::string const match_definition_standin::name("match_definition");
	
struct root_scan_stackable : scan_stackable
{
	std::shared_ptr<luxem::value> &root;
	match_map matches;

	root_scan_stackable(std::shared_ptr<luxem::value> &root) : root(root) {}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (last_result == step_push)
		{
			if (context.verbose) std::cerr << "Scanning " << root->get_name() << std::endl;
			last_result = scan_node(context, matches, true, root, context.reverse ? context.to : context.from);
			if (last_result == step_push) return step_push;
		}

		if (last_result == step_fail) 
		{
			if (context.verbose) std::cerr << "Failed to match " << root->get_name() << std::endl;
			return step_break;
		}

		if (context.verbose) std::cerr << "Matched " << root->get_name() << ", transforming" << std::endl;
		transform_root(matches, root, context.reverse ? context.from : context.to, context.verbose);
		return step_break;
	}
};

struct object_scan_stackable : scan_stackable
{
	match_map &matches;
	bool root;
	luxem::object_value &target, &from;

	luxem::object_value::object_data::iterator iterator;
	bool first_wind;

	object_scan_stackable(match_map &matches, bool root, luxem::object_value &target, luxem::object_value &from) :
		matches(matches),
		root(root),
		target(target),
		from(from),
		iterator(from.get_data().begin()),
		first_wind(true)
		{}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (first_wind)
		{
			auto fail_first_wind = [this](void)
			{
				if (root)
				{
					iterator = target.get_data().begin();
					first_wind = false;
					return step_continue;
				}
				else return step_fail;
			};
			if (last_result == step_fail) return fail_first_wind();
			if (iterator == from.get_data().end()) return step_break;
			auto target_found = target.get_data().find(iterator->first);
			if (target_found == target.get_data().end()) return fail_first_wind();
			auto result = scan_node(context, matches, false, target_found->second, iterator->second);
			if (result == step_fail) return fail_first_wind();
			++iterator;
			return result;
		}
		else
		{
			if (iterator == target.get_data().end()) return step_fail;
			context.stack.emplace_back(std::make_unique<root_scan_stackable>(iterator->second));
			++iterator;
			return step_push;
		}
	}
};

struct array_scan_stackable : scan_stackable
{
	match_map &matches;
	bool root;
	luxem::array_value &target, &from;

	luxem::array_value::array_data::iterator target_iterator, from_iterator;
	bool first_wind;

	array_scan_stackable(match_map &matches, bool root, luxem::array_value &target, luxem::array_value &from) :
		matches(matches),
		root(root),
		target(target),
		from(from),
		target_iterator(target.get_data().begin()),
		from_iterator(from.get_data().begin()),
		first_wind(true)
		{}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (first_wind)
		{
			auto fail_first_wind = [this](void)
			{
				if (root)
				{
					target_iterator = target.get_data().begin();
					first_wind = false;
					return step_continue;
				}
				else return step_fail;
			};
			if (last_result == step_fail) return fail_first_wind();
			if (from_iterator == from.get_data().end()) return step_break;
			auto result = scan_node(context, matches, false, *target_iterator, *from_iterator);
			if (result == step_fail) return fail_first_wind();
			++target_iterator;
			++from_iterator;
			return result;
		}
		else
		{
			if (target_iterator == target.get_data().end()) return step_fail;
			context.stack.emplace_back(std::make_unique<root_scan_stackable>(*target_iterator));
			++target_iterator;
			return step_continue;
		}
	}
};

step_result scan_node(
	scan_context &context, 
	match_map &matches, 
	bool is_root, 
	std::shared_ptr<luxem::value> &target,
	std::shared_ptr<luxem::value> const &from)
{
	if (context.verbose) std::cerr << "Comparing " << target->get_name() << " to " << from->get_name() << std::endl;
	if (from->is<luxem::primitive_value>())
	{
		if (!target->is<luxem::primitive_value>()) return step_fail;
		auto &from_resolved = from->as<luxem::primitive_value>();
		auto &target_resolved = target->as<luxem::primitive_value>();
		if (target_resolved.has_type() != from_resolved.has_type()) return step_fail;
		if (from_resolved.has_type() && (target_resolved.get_type() != from_resolved.get_type())) return step_fail;
		if (target_resolved.get_primitive() != from_resolved.get_primitive()) return step_fail;
	}
	else if (from->is<luxem::object_value>())
	{
		if (!target->is<luxem::object_value>()) return step_fail;
		auto &from_resolved = from->as<luxem::object_value>();
		auto &target_resolved = target->as<luxem::object_value>();
		if (from_resolved.get_data().size() != target_resolved.get_data().size()) return step_fail;
		context.stack.push_back(std::make_unique<object_scan_stackable>(
			matches, 
			is_root, 
			target_resolved,
			from_resolved));
		return step_push;
	}
	else if (from->is<luxem::array_value>())
	{
		if (!target->is<luxem::array_value>()) return step_fail;
		auto &from_resolved = from->as<luxem::array_value>();
		auto &target_resolved = target->as<luxem::array_value>();
		if (from_resolved.get_data().size() != target_resolved.get_data().size()) return step_fail;
		context.stack.push_back(std::make_unique<array_scan_stackable>(
			matches, 
			is_root, 
			target_resolved,
			from_resolved));
		return step_push;
	}
	else if (from->is<match_definition_standin>())
	{
		auto &definition = from->as<match_definition_standin>();
		return definition->scan(context, matches, target);
	}
	else
	{
		assert(false);
		return step_fail;
	}
	return step_break;
}

struct object_transform_stackable : transform_stackable
{
	std::shared_ptr<luxem::object_value> out;
	luxem::object_value const &to;

	luxem::object_value::object_data::const_iterator to_iterator;

	object_transform_stackable(std::shared_ptr<luxem::object_value> out, luxem::object_value &to) : 
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
	std::shared_ptr<luxem::array_value> out;
	luxem::array_value const &to;

	luxem::array_value::array_data::const_iterator to_iterator;

	array_transform_stackable(std::shared_ptr<luxem::array_value> out, luxem::array_value &to) : 
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
	if (to->is<luxem::primitive_value>())
	{
		auto out = std::make_shared<luxem::primitive_value>(to->as<luxem::primitive_value>().get_primitive());
		if (to->has_type()) out->set_type(to->get_type());
		return out;
	}
	else if (to->is<luxem::object_value>())
	{
		auto out = std::make_shared<luxem::object_value>();
		if (to->has_type()) out->set_type(to->get_type());
		context.stack.emplace_back(std::make_unique<object_transform_stackable>(out, to->as<luxem::object_value>()));
		return out;
	}
	else if (to->is<luxem::array_value>())
	{
		auto out = std::make_shared<luxem::array_value>();
		if (to->has_type()) out->set_type(to->get_type());
		context.stack.emplace_back(std::make_unique<array_transform_stackable>(out, to->as<luxem::array_value>()));
		return out;
	}
	else if (to->is<match_definition_standin>())
	{
		return to->as<match_definition_standin>()->generate(context, matches);
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

namespace luxemog
{

transform::transform(std::shared_ptr<luxem::value> &&data, bool verbose) : 
	verbose(verbose), 
	from_can_be_from(true), 
	to_can_be_from(true)
{
	auto &object = data->as<luxem::reader::object_context>();

	struct context_type
	{
		bool root;
		std::map<std::string, std::shared_ptr<match_definition>> match_definitions;

		std::shared_ptr<match_definition> get_definition(std::string const &name)
		{
			auto found = match_definitions.find(name);
			if (found != match_definitions.end())
			{
				return found->second;
			}
			else
			{
				auto definition = std::make_shared<match_definition>();
				definition->id = name;
				match_definitions.emplace(name, definition);
				return definition;
			}
		}

		std::shared_ptr<luxem::value> match_from_object(std::shared_ptr<luxem::value> &data)
		{
			struct match_context_type
			{
				std::string id;
			};

			auto &object_data = data->as<luxem::reader::object_context>();
			auto standin = std::make_shared<match_definition_standin>();
			auto match_context = std::make_shared<match_context_type>();
			object_data.element("name", [match_context](std::shared_ptr<luxem::value> &&data)
			{
				match_context->id = data->as<luxem::primitive_value>().get_primitive();
			});
			object_data.finally([&, match_context, standin](void)
			{
				if (match_context->id.empty()) throw std::runtime_error("Missing match name.");
				*standin = get_definition(match_context->id);
			});
			
			return standin;
		};
	};
	auto context = std::make_shared<context_type>();

	auto process = [context](std::string const &key, std::shared_ptr<luxem::value> &data)
	{
		if (data->has_type())
		{
			if (data->get_type() == "*match")
			{
				if (data->is<luxem::primitive_value>())
				{
					auto &name = data->as<luxem::primitive_value>().get_primitive();
					data = std::make_shared<match_definition_standin>(context->get_definition(name));
				}
				else if (data->is<luxem::reader::object_context>())
				{
					data = context->match_from_object(data);
				}
				else throw std::runtime_error("*match definitions must be strings or objects.");
			}
			else if (data->get_type().substr(0, 1) == "*")
			{
				data->set_type(data->get_type().substr(1));
			}
		}
	};

	object.element("matches", [this, context](std::shared_ptr<luxem::value> &&data)
	{
		data->as<luxem::reader::array_context>().element([this, context](std::shared_ptr<luxem::value> &&data)
		{
			context->match_from_object(data);
		});
	});
	object.build_struct(
		"from", 
		[this](std::shared_ptr<luxem::value> &&data) 
		{ 
			from = std::move(data); 
			if (from->is<match_definition_standin>()) 
			{ 
				from_can_be_from = false; 
				if (this->verbose) 
					std::cerr << "'from' root element is a *match; disabling as source pattern." << std::endl;
			}
		}, 
		process // Double wrap to do from-specific stuff
	);
	object.build_struct(
		"to", 
		[this](std::shared_ptr<luxem::value> &&data) 
		{ 
			to = std::move(data); 
			if (to->is<match_definition_standin>()) 
			{ 
				to_can_be_from = false; 
				if (this->verbose) 
					std::cerr << "'to' root element is a *match; disabling as source pattern." << std::endl;
			}
		}, 
		process // Double wrap to do from-specific stuff
	);
}

void transform::apply(std::shared_ptr<luxem::value> &target, bool reverse)
{
	if (!reverse && !from_can_be_from) 
		throw std::runtime_error("'from' cannot be used as source pattern in forward transformation.");
	if (reverse && !to_can_be_from) 
		throw std::runtime_error("'to' cannot be used as source pattern in reverse transformation.");
	scan_context context{verbose, reverse, from, to};

	size_t count = 0;
	context.stack.push_back(std::make_unique<root_scan_stackable>(target));
	step_result last_result = step_push;
	while (!context.stack.empty())
	{
		++count;
		if (count > 1000) assert(false);
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

