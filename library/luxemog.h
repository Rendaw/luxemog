#ifndef luxemog_h
#define luxemog_h

#include <luxem-cxx/luxem.h>

namespace luxemog
{

struct transform
{
	transform(std::shared_ptr<luxem::value> &&root, bool verbose = false);
	void apply(std::shared_ptr<luxem::value> &target, bool reverse = false);

	private:
		bool verbose;
		std::shared_ptr<luxem::value> from, to;
		bool from_can_be_from;
		bool to_can_be_from;
};

struct transform_list
{
	transform_list(bool verbose = false);
	void deserialize(std::shared_ptr<luxem::value> &&root);

	void apply(std::shared_ptr<luxem::value> &target, bool reverse = false);

	private:
		bool verbose;
		std::list<std::unique_ptr<transform>> transforms;
};

}

#endif

