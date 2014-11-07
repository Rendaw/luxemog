#ifndef luxemog_h
#define luxemog_h

#include <luxem-cxx/luxem.h>

namespace luxemog
{

struct transform
{
	transform(std::shared_ptr<luxem::value> &&root, bool verbose = false);
	void apply(std::shared_ptr<luxem::value> &target, bool reverse = false);

	struct transform_data // Internal only, basically private
	{
		transform_data(std::shared_ptr<luxem::value> &&root);
		std::shared_ptr<luxem::value> from, to;
		std::list<std::unique_ptr<transform_data>> subtransforms;
	};

	private:
		bool verbose;

		transform_data data;
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

