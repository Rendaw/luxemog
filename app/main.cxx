#include <getopt.h>
#include <iostream>

#include "../library/luxemog.h"

// TODOish getopt_long won't work on Windows with wmain, so I haven't even attempted to get unicode filenames to work in that environment.

int main(int argc, char **argv)
{
	bool verbose = false;
	bool reverse = false;
	bool minimize = true;
	bool use_spaces = false;
	int indent_count = 1;
	std::string transforms_filename, source_filename, dest_filename;

	{
		option long_options[] = {
			{"help", no_argument, 0, 'h'},
			{"verbose", no_argument, 0, 'v'},
			{"out", required_argument, 0, 'o'},
			{"reverse", no_argument, 0, 'r'},
			{"minimize", no_argument, 0, 'm'},
			{"use-spaces", no_argument, 0, 's'},
			{"indent-count", required_argument, 0, 'i'},
			{0, 0, 0, 0}
		};
		int positional_index = 0;

		int next;
		while ((next = getopt_long(argc, argv, "hvo:rmsi:", long_options, &positional_index)) != -1) 
		{
			switch (next) 
			{
				case 'h': 
					std::cout << 
"usage: luxemog OPTIONS TRANSFORMS SOURCE\n"
"\n"
"    OPTIONS can be any combination or none of the following:\n"
"      -h, --help                      Show this message.\n"
"      -v, --verbose                   Write diagnostic messages to stderr.\n"
"      -o FILE, --out FILE             Write the result to FILE rather than\n"
"                                      stdout.  If '-', use stdout.\n"
"      -r, --reverse                   Reverse 'to' and 'from' patterns.\n"
"      -m, --minimize                  Don't insert whitespace to prettify\n"
"                                      output.\n"
"      -s, --use-spaces                Use spaces instead of tabs to prettify\n"
"                                      output\n"
"      -i COUNT, --indent-count COUNT  Use COUNT spaces or tabs to indent\n"
"                                      pretty output\n"
"\n"
"    TRANSFORMS\n"
"      A filename.\n"
"\n"
"    SOURCE\n"
"      A filename or '-' for stdin.\n"
"\n"
"Transforms SOURCE based on the transformations in the TRANSFORMS file.\n" << std::endl;
					break;
				case 'v': verbose = true; break;
				case 'o': dest_filename = optarg; break;
				case 'r': reverse = true; break;
				case 'm': minimize = true; break;
				case 's': use_spaces = true; break;
				case 'i': indent_count = atoi(optarg); break;
			}
		}

		if (argc - positional_index < 2)
		{
			std::cerr << "Missing one or more of: TRANSFORMS, SOURCE" << std::endl;
			return 1;
		}

		transforms_filename = argv[positional_index++];
		source_filename = argv[positional_index++];
	}

	luxemog::transform_list transforms;

	try
	{
		luxem::reader reader;
		reader.element([&](std::shared_ptr<luxem::value> &&data)
		{
			if (!data->has_type()) throw std::runtime_error("Missing version.");
			if (data->get_type() != "luxemog 0.0.1") 
			{
				std::stringstream message;
				message << "Unknown version " << data->get_type();
				throw std::runtime_error(message.str());
			}
			transforms.deserialize(std::move(data));
		});

		auto transform_file = fopen(transforms_filename.c_str(), "rb");
			
		if (!transform_file)
		{
			std::cerr << "Failed to open TRANSFORMS file " << transforms_filename << std::endl;
			return 1;
		}

		{
			luxem::finally finally([&](void) { fclose(transform_file); });
			reader.feed(transform_file);
		}
	}
	catch (std::exception &exception)
	{
		std::cerr << "Error loading TRANSFORMS from " << transforms_filename << ": " << exception.what() << std::endl;
		return 1;
	}

	std::vector<std::shared_ptr<luxem::value>> trees;

	try
	{
		if (source_filename == "-") { trees = luxem::read_struct(stdin); }
		else
		{
			auto source_file = fopen(source_filename.c_str(), "rb");
			
			if (!source_file)
			{
				std::cerr << "Failed to open SOURCE file " << source_filename << std::endl;
				return 1;
			}

			{
				luxem::finally finally([&](void) { fclose(source_file); });
				trees = luxem::read_struct(source_file);
			}
		}
	}
	catch (std::exception &exception)
	{
		std::cerr << "Error loading SOURCE from " << source_filename << ": " << exception.what() << std::endl;
		return 1;
	}

	try
	{
		for (auto &tree : trees) 
			transforms.apply(tree);
	}
	catch (std::exception &exception)
	{
		std::cerr << "Error performing transformation: " << exception.what() << std::endl;
		return 1;
	}

	try
	{
		auto write = [&](luxem::writer &&writer)
		{
			if (!minimize)
				writer.set_pretty(use_spaces ? ' ' : '\t', indent_count);
			for (auto &tree : trees) writer.value(*tree);
		};

		if (dest_filename.empty() || dest_filename == "-")
		{
			write({stdout});
		}
		else
		{
			auto dest_file = fopen(dest_filename.c_str(), "rb");

			if (!dest_file)
			{
				std::cerr << "Failed to open output file " << dest_filename << std::endl;
				return 1;
			}

			{
				luxem::finally finally([&](void) { fclose(dest_file); });
				write({dest_file});
			}
		}
	}
	catch (std::exception &exception)
	{
		std::cerr << 
			"Error writing to " << (dest_filename.empty() ? std::string("-") : dest_filename) << 
			": " << exception.what() << 
			std::endl;
		return 1;
	}

	return 0;
}
