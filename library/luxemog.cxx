#include "luxemog.h"

typedef std::map<std::string, std::shared_ptr<luxem::value>> match_map;

struct scan_context;
struct scan_stackable
{
	virtual ~scan_stackable(void);
	virtual step_result step(scan_context &context, step_result last_result) = 0;
};

struct scan_context
{
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
	std::list<std::unique_ptr<transform_stackable>> stack;
};

/*bool scan_node(
	scan_context &context, 
	match_map &matches, 
	std::shared_ptr<luxem::value> target, 
	std::shared_ptr<luxem::value> from);*/

/*std::shared_ptr<luxem::value> transform_node(
	transform_context &context, 
	match_map &matches, 
	std::shared_ptr<luxem::value> to)*/

struct match_definition : luxem::value
{
	std::string name;

	bool scan(match_map &matches, scan_stack &stack, std::shared_ptr<luxem::value> &target)
	{
		auto found = matches.find(name);
		if (found != matches.end())
		{
			std::stringstream message;
			message << "Match " << name << " matched multiple times.  Matches must only occur once.";
			throw std::runtime_error(message.str());
		}
		matches.emplace_back(name, target);
		stack.emplace_back(std::make_unique<root_scan_stackable>(target));
		return true;
	}

	std::shared_ptr<luxem::value> generate(transform_context &context, match_map &matches)
	{
		auto found = matches.find(name);
		if (found == matches.end())
		{
			std::stringstream message;
			message << "Match " << name << ", required by output, is missing.";
			throw std::runtime_error(message.str());
		}
		return transform_node(context, matches, found->second);
	}
};
	
struct root_scan_stackable : scan_stackable
{
	std::shared_ptr<luxem::value> &root;
	match_map matches;

	root_scan_stackable(std::shared_ptr<luxem::value> &root) : root(root) {}

	step_result step(scan_context &context, step_result last_result) override
	{
		if (last_result == step_push)
		{
			last_result = scan_node(context, matches, root, context->from);
			if (last_result == step_push) return step_push;
		}

		if (last_result == step_fail) return step_break;

		transform(context, matches, root, context->from);
		return step_break;
	}
};

struct object_scan_stackable : scan_stackable
{
	match_map &matches;
	bool root_scan_stackable;
	luxem::object_value &from, &target;

	luxem::object_value::object_data::iterator iterator;
	bool first_wind;

	object_scan_stackable(match_map &matches, bool root, luxem::object_value &target, luxem::object_value &from) :
		matches(matches),
		root_scan_stackable(root),
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
				if (root_scan_stackable)
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
			auto result = scan_node(context, matches, target_found->second, iterator->second);
			if (result == scan_fail) return fail_first_wind();
			++iterator;
			return result;
		}
		else
		{
			if (iterator == target.get_data().end()) return step_break;
			context.stack.emplace_back(std::make_unique<root_scan_stackable>(*iterator));
			++iterator;
			return step_push;
		}
	}
};

struct array_scan_stackable : scan_stackable
{
	match_map &matches;
	bool root_scan_stackable;
	std::shared_ptr<luxem::value> &target;
	luxem::array_value &from;

	luxem::array_value::array_data::iterator target_iterator, from_iterator;
	bool first_wind;

	array_scan_stackable(match_map &matches, bool root, std::shared_ptr<luxem::value> &target, luxem::array_value &from) :
		matches(matches),
		root_scan_stackable(root),
		target(target),
		from(from),
		target_iterator(target->as<luxem::array_value>().get_data().begin()),
		from_iterator(from->get_data().begin()),
		first_wind(true)
		{}

	step_result step(match_map &matches, scan_stack &stack, step_result last_result) override
	{
		if (first_wind)
		{
			auto fail_first_wind = [this](void)
			{
				if (root_scan_stackable)
				{
					target_iterator = target.get_data().begin();
					first_wind = false;
					return step_continue;
				}
				else return step_fail;
			};
			if (last_result == step_fail) return fail_first_wind();
			if (from_iterator == from.get_data().end()) return step_break;
			auto result = scan_node(matches, stack, *target_iterator, from_iterator);
			if (result == scan_fail) return fail_first_wind();
			++target_iterator;
			++from_iterator;
			return result;
		}
		else
		{
			if (target_iterator == target.get_data().end()) return step_break;
			context.stack.emplace_back(std::make_unique<root_scan_stackable>(*target_iterator));
			++target_iterator;
			return step_continue;
		}
	}
};

bool scan_node(scan_context &context, match_map &matches, std::shared_ptr<luxem::value> target, std::shared_ptr<luxem::value> from)
{
	if (from->is<luxem::primitive_value>())
	{
		if (!target->is<luxem::primitive_value()) return step_fail;
		auto from_resolved = from->as<luxem::primitive_value>();
		auto target_resolved = target->as<luxem::primitive_value>();
		if (target_resolved->has_type() != from_resolved->has_type()) return step_fail;
		if (target_resolved->get_type() != from_resolved->get_type()) return step_fail;
		if (target_resolved->get_resolved() != from_resolved->get_resolved()) return step_fail;
	}
	else if (from->is<luxem::object_value>())
	{
		if (!target->is<luxem::object_value()) return step_fail;
		auto from_resolved = from->as<luxem::object_value>();
		auto target_resolved = target->as<luxem::object_value>();
		if (from_resolved->get_data().size() != target_resolved->get_data().size()) return step_fail;
		stack.push_back(std::make_unique<object_scan_stackable>(matches, from_resolved, target_resolved));
		return step_push;
	}
	else if (from->is<luxem::array_value>())
	{
		if (!target->is<luxem::array_value()) return step_fail;
		auto from_resolved = from->as<luxem::array_value>();
		auto target_resolved = target->as<luxem::array_value>();
		if (from_resolved->get_data().size() != target_resolved->get_data().size()) return step_fail;
		stack.push_back(std::make_unique<object_scan_stackable>(matches, from_resolved, target_resolved));
		return step_push;
	}
	else if (from->is<match_definition>())
	{
		auto definition = from->as<match_definition>();
		return definition->scan(target);
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
	match_map &matches;
	std::shared_ptr<luxem::object_value> out;
	luxem::object_value &to;

	luxem::object_value::iterator to_iterator;

	object_transform_stackable(match_map &matches, std::shared_ptr<luxem::object_value> out, luxem::object_value &to) : 
		matches(matches),
		out(out), 
		to(to), 
		to_iterator(to.get_object().begin()) 
		{}

	bool step(transform_context &context) override
	{
		if (to_iterator == to.get_data().end()) return false;
		out.emplace(to_iterator->first, transform(context, matches, to_iterator->second));
		++to_iterator;
		return true;
	}
};

struct array_transform_stackable : transform_stackable
{
	match_map &matches;
	std::shared_ptr<luxem::array_value> out;
	luxem::array_value &to;

	luxem::array_value::iterator to_iterator;

	array_transform_stackable(match_map &matches, std::shared_ptr<luxem::array_value> out, luxem::array_value &to) : 
		matches(matches),
		out(out), 
		to(to), 
		to_iterator(to.get_array().begin()) 
		{}

	bool step(transform_context &context) override
	{
		if (to_iterator == to.get_data().end()) return false;
		out.emplace(transform_node(context, matches, to_iterator->second));
		++to_iterator;
		return true;
	}
};

std::shared_ptr<luxem::value> transform_node(
	transform_context &context, 
	match_map &matches, 
	std::shared_ptr<luxem::value> to)
{
	if (to->is<luxem::primitive_value>())
	{
		return to;
	}
	else if (to->is<luxem::object_value>())
	{
		auto out = to->has_type() ? 
			std::make_shared<luxem::object_value>(to->get_type()) : 
			std::make_shared<luxem::object_value>();
		stack.emplace_back(std::make_unique<object_transform_stackable>(out, *to->as<luxem::object_value>()));
		return out;
	}
	else if (to->is<luxem::array_value>())
	{
		auto out = to->has_type() ? 
			std::make_shared<luxem::array_value>(to->get_type()) : 
			std::make_shared<luxem::array_value>();
		stack.emplace_back(std::make_unique<array_transform_stackable>(out, *to->as<luxem::array_value>()));
		return out;
	}
	else if (to->is<match_definition>())
	{
		return to->as<match_definition>()->generate(matches);
	}
	else
	{
		assert(false);
		return {};
	}
}

void transform(
	match_map &matches, 
	std::shared_ptr<luxem::value> &root,
	std::shared_ptr<luxem::value> const &to)
{
	transform_context context;
	root = transform_node(to);
	while (!stack.empty()) 
		if (!stack.back()->step(context, last_result))
			stack.pop_back();
}

namespace luxemog
{

transformation::transformation(std::shared_ptr<luxem::reader::object_context> &&object)
{
	struct context_type
	{
		std::map<std::string, std::shared_ptr<match_definition>> match_definitions;
	};
	auto context = std::make_shared<context_type>();

	struct match_context_type
	{
		std::string name;
	};

	auto deserialize_match = [](auto &definitions, luxem::reader::object_context &object)
	{
		auto match_context = std::make_shared<match_context_type>();
		object_data.element("name", [match_context](std::shared_ptr<luxem::value> &&data)
		{
			data.assert_primitive();
			match_context->name = data->as<luxem::primitive_value>().get_primitive();
		});
		object_data.finished([&, match_context](void)
		{
			if (match_context->name.empty()) throw std::runtime_exception("Missing match name.");
			auto definition = get_definition(name);
			data = definition;
		});
	};

	std::function<void(std::string &key, std::shared_ptr<luxem::value> &data)> process;
	context->process = [context](std::string &key, std::shared_ptr<luxem::value> &data)
	{
		if (data->has_type())
		{
			if (data->get_type() == "*match")
			{
				auto get_definition = [&](std::string const &name)
				{
					auto found = context->match_definitions.find(name);
					if (found != context->match_definitions.end())
					{
						return found->second;
					}
					else
					{
						auto definition = std::make_shared<match_definition>();
						context->match_definitions.emplace(name, definition);
						return definition;
					}
				};

				if (data->is<luxem::primitive_value>())
				{
					auto name = data->as<luxem::primitive_value>().get_primitive();
					data = get_definition(name);
				}
				else if (data->is<luxem::reader::object_context>())
				{
					auto object_data = data->as<luxem::reader::object_context>();
					deserialize_match(match_definitions, object_data);
				}
				else throw std::runtime_exception("Matches must be strings or objects.");
			}
			else if (data->get_type().substr(0, 1) == "*")
			{
				data->set_type(data->get_type().substr(1));
			}
		}
	};

	object.element("matches", [this](std::shared_ptr<luxem::value> &&data)
	{
		data->assert_object();
		auto match_context = std::make_shared<match_context_type>();
		object_data.element("name", [match_context](std::shared_ptr<luxem::value> &&data)
		{
			data.assert_primitive();
			match_context->name = data->as<luxem::primitive_value>().get_primitive();
		});
		object_data.finished([&, match_context](void)
		{
			if (match_context->name.empty()) throw std::runtime_exception("Missing match name.");
			auto definition = get_definition(name);
			data = definition;
		});
	});
	object.element(
		"from", 
		[this](std::shared_ptr<luxem::value> &&data) { from = std::move(data); }, 
		process
	});
	object.element(
		"to", 
		[this](std::shared_ptr<luxem::value> &&data) { to = std::move(data); }, 
		process
	});
}

void transformation::apply(std::shared_ptr<luxem::value> &target)
{
	scan_context context;

	stack.push_back(std::make_unique<root_scan_stackable>(target));
	step_result last_result = step_push;
	while (!stack.empty())
	{
		last_result = stack.back()->step(context, last_result);
		switch (last_result)
		{
			case step_fail: assert(false); stack.pop_back(); break;
			case step_break: stack.pop_back(); break;
			default: break;
		}
	}
	return target;
}

}

