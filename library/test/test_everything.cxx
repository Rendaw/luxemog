#include "../luxemog.h"

#include <iostream>
#include <memory>

template <typename type> void assert1(type const &value)
{
	std::cout << "Assert true: " << value << std::endl;
	assert(value);
}

template <typename type> void assert2(type const &got, type const &expected)
{
	std::cout << "Expected: " << expected << std::endl;
	std::cout << "Got     : " << got << std::endl;
	assert(got == expected);
}

void compare_value(luxem::value const &got, luxem::value const &expected)
{
	assert2(got.has_type(), expected.has_type());
	if (got.has_type()) assert2(got.get_type(), expected.get_type());
	if (expected.is<luxem::object>())
	{
		auto expected_object = expected.as<luxem::object>();
		auto got_object = got.as<luxem::object>();
		assert2(got_object.get_data().size(), expected_object.get_data().size());
		for (auto &expected_pair : expected_object.get_data())
		{
			auto got_pair = got_object.get_data().find(expected_pair.first);
			assert(got_pair != got_object.get_data().end());
			compare_value(*got_pair->second, *expected_pair.second);
		}
	}
	else if (expected.is<luxem::array>())
	{
		auto expected_array = expected.as<luxem::array>();
		auto got_array = got.as<luxem::array>();
		assert2(got_array.get_data().size(), expected_array.get_data().size());
		for (size_t index = 0; index < expected_array.get_data().size(); ++index)
			compare_value(*got_array.get_data()[index], *expected_array.get_data()[index]);
	}
	else if (expected.is<luxem::primitive>())
	{
		auto expected_primitive = expected.as<luxem::primitive>();
		auto got_primitive = got.as<luxem::primitive>();
		assert2(got_primitive.get_primitive(), expected_primitive.get_primitive());
	}
	else assert(false);
}


std::unique_ptr<luxemog::transform_list> make_transforms(std::string const &text)
{
	auto transforms = std::make_unique<luxemog::transform_list>(true);
	luxem::reader reader;
	reader.element([&transforms](std::shared_ptr<luxem::value> &&value) mutable 
		{ transforms->deserialize(std::move(value)); });
	reader.feed(text);
	return std::move(transforms);
}

void test(std::string const &transform_source, std::string const &source, std::string const &expected)
{
	auto transforms = make_transforms(transform_source);
	std::shared_ptr<luxem::value> working_tree, expected_tree;
	
	{
		luxem::reader reader;
		reader.build_struct([&](std::shared_ptr<luxem::value> &&value) mutable 
			{ working_tree = std::move(value); });
		reader.feed(source);
	}

	{
		luxem::reader reader;
		reader.build_struct([&](std::shared_ptr<luxem::value> &&value) mutable 
			{ expected_tree = std::move(value); });
		reader.feed(expected);
	}

	transforms->apply(working_tree);

	std::cout << "\nGot:\n" << luxem::writer().set_pretty().value(*working_tree).dump() <<
		"\nExpected:\n" << luxem::writer().set_pretty().value(*expected_tree).dump() << std::endl;

	compare_value(*working_tree, *expected_tree);
}

void test_primitives(void)
{
	test
	(
		"["
			"{"
				"from: 4,"
				"to: 5,"
			"},"
		"]",
		"4",
		"5"
	);
	
	test
	(
		"["
			"{"
				"from: 22,"
				"to: 23,"
			"},"
		"]",
		"291",
		"291"
	);
	
	test
	(
		"["
			"{"
				"from: (int) 6,"
				"to: (dog) 6,"
			"},"
		"]",
		"(int) 6",
		"(dog) 6"
	);
	
	test
	(
		"["
			"{"
				"from: (int) 327,"
				"to: (dog) 33,"
			"},"
		"]",
		"327",
		"327"
	);
	
	test
	(
		"["
			"{"
				"from: 3838,"
				"to: 94,"
			"},"
		"]",
		"(int) 3838",
		"(int) 3838"
	);
	
}

void test_objects(void)
{
	test
	(
		"["
			"{"
				"from: {},"
				"to: 7,"
			"},"
		"]",
		"{}",
		"7"
	);

	test
	(
		"["
			"{"
				"from: (int) {},"
				"to: 7,"
			"},"
		"]",
		"(int) {}",
		"7"
	);
	
	test
	(
		"["
			"{"
				"from: (int) {},"
				"to: 7,"
			"},"
		"]",
		"(qog) {}",
		"(qog) {}"
	);
	
	test
	(
		"["
			"{"
				"from: {key: val},"
				"to: -2,"
			"},"
		"]",
		"{key: val}",
		"-2"
	);

	test
	(
		"["
			"{"
				"from: {key: val},"
				"to: -74,"
			"},"
		"]",
		"{key: val, card: cad}",
		"{key: val, card: cad}"
	);
	
	test
	(
		"["
			"{"
				"from: {key: vole},"
				"to: -55,"
			"},"
		"]",
		"{key: val}",
		"{key: val}"
	);
}

void test_arrays(void)
{
	test
	(
		"["
			"{"
				"from: [],"
				"to: 9,"
			"},"
		"]",
		"[]",
		"9"
	);
	
	test
	(
		"["
			"{"
				"from: (int) [],"
				"to: 7,"
			"},"
		"]",
		"(int) []",
		"7"
	);
	
	test
	(
		"["
			"{"
				"from: (int) [],"
				"to: 7,"
			"},"
		"]",
		"(qog) []",
		"(qog) []"
	);
	
	test
	(
		"["
			"{"
				"from: [2, 5],"
				"to: 333,"
			"},"
		"]",
		"[2, 5]",
		"333"
	);
	
	test
	(
		"["
			"{"
				"from: [2],"
				"to: 334,"
			"},"
		"]",
		"[2, 5]",
		"[2, 5]"
	);
}

void test_wildcards(void)
{
	test
	(
		"["
			"{"
				"from: (*wild) {},"
				"to: 5,"
			"},"
		"]",
		"4",
		"5"
	);
}

void test_match(void)
{
	test
	(
		"["
			"{"
				"from: (*match) {"
					"id: w/e,"
					"pattern: 4,"
				"},"
				"to: 5,"
			"},"
		"]",
		"4",
		"5"
	);
	
	test
	(
		"["
			"{"
				"from: (*match) {"
					"id: w/e,"
					"pattern: 4,"
				"},"
				"to: 5,"
			"},"
		"]",
		"33",
		"33"
	);

	test
	(
		"["
			"{"
				"from: (*match) w/e,"
				"to: 5,"
			"},"
		"]",
		"251",
		"5"
	);
	
}

void test_match_with_wildcard(void)
{
	test
	(
		"["
			"{"
				"from: ["
					"(*match) w/e,"
					"735,"
				"],"
				"to: ["
					"(*match) w/e,"
					"28,"
				"],"
			"},"
		"]",
		"["
			"["
				"22,"
				"735,"
			"],"
			"735,"
		"]",
		"["
			"["
				"22,"
				"28,"
			"],"
			"28,"
		"]"
	);
}

void test_errors(void)
{
	try
	{
		test
		(
			"["
				"{"
					"from: 9,"
					"to: (*error) testing,"
				"},"
			"]",
			"9",
			"IGNORED"
		);
		assert(false);
	}
	catch (std::runtime_error &error) {}
}

void test_alts(void)
{
	test
	(
		"["
			"{"
				"from: (*alt) [1, 7],"
				"to: 9,"
			"},"
		"]",
		"7",
		"9"
	);
	
	test
	(
		"["
			"{"
				"from: (*alt) [1, 7],"
				"to: 9,"
			"},"
		"]",
		"2",
		"2"
	);

	test
	(
		"["
			"{"
				"from: (*alt) ["
					"[(*match) nomatch, 4],"
					"[22, 5],"
				"],"
				"to: (*match) nomatch,"
			"},"
		"]",
		"[22, 4]",
		"22"
	);
	
	try
	{
		test
		(
			"["
				"{"
					"from: (*alt) ["
						"[(*match) nomatch, 4],"
						"[22, 5],"
					"],"
					"to: (*match) nomatch,"
				"},"
			"]",
			"[22, 5]",
			"IGNORED"
		);
		assert(false);
	}
	catch (std::runtime_error &error) {}
}

void test_subtransforms(void)
{
	test
	(
		"["
			"{"
				"from: {x: (*match) value},"
				"subtransforms: ["
					"{"
						"from: 7,"
						"to: 9,"
					"},"
				"],"
			"},"
		"]",
		"{x: 7}",
		"{x: 9}"
	);
	
	test
	(
		"["
			"{"
				"from: {x: (*match) value},"
				"subtransforms: ["
					"{"
						"from: 7,"
						"to: 9,"
					"},"
				"],"
			"},"
		"]",
		"{y: 7}",
		"{y: 7}"
	);
}

void test_regexes(void)
{
	test
	(
		"["
			"{"
				"from: (*regex) \"[[:digit:]]\","
				"to: 5,"
			"},"
		"]",
		"4",
		"5"
	);
	
	test
	(
		"["
			"{"
				"from: (*regex) \"[[:digit:]]\","
				"to: 5,"
			"},"
		"]",
		"a",
		"a"
	);
	
	test
	(
		"["
			"{"
				"from: (*type_regex) {"
					"exp: [\"[[:digit:]]\"],"
					"value: (*wild),"
				"},"
				"to: 5,"
			"},"
		"]",
		"(4) lemonade",
		"5"
	);
	
	test
	(
		"["
			"{"
				"from: (*type_regex) {"
					"exp: [\"[[:digit:]]\"],"
					"value: (*wild),"
				"},"
				"to: 5,"
			"},"
		"]",
		"(a) asparagus",
		"(a) asparagus"
	);
}

void test_format(void)
{
	test
	(
		"["
			"{"
				"from: (*regex) {id: g, exp: .*},"
				"to: (*string) \"<g>4\","
			"},"
		"]",
		"\"hello friend\"",
		"\"hello friend4\""
	);

	test
	(
		"["
			"{"
				"from: (*wild),"
				"to: (*string) \"%<g>4\","
			"},"
		"]",
		"frog",
		"<g>4"
	);
	
	test
	(
		"["
			"{"
				"from: (*regex) {id: goose, exp: .*},"
				"to: (*string) \"%%<goose> berry\","
			"},"
		"]",
		"frog",
		"\"%frog berry\""
	);

	test
	(
		"["
			"{"
				"from: (*wild),"
				"to: (*type) {"
					"format: \"dog adhesive\","
					"value: [],"
				"},"
			"},"
		"]",
		"IGNORE",
		"(dog adhesive) []"
	);
}

int main(void)
{
	test_primitives();
	test_objects();
	test_arrays();
	test_wildcards();
	test_match();
	test_match_with_wildcard();
	test_errors();
	test_alts();
	test_subtransforms();
	test_regexes();
	test_format();

	return 0;
}

