
#include "coreio.h"
#include "stdio.h"

#include <pspkernel.h>

struct CoreFile
{
	size_t seek_ptr = 0;

	virtual void seek() = 0;
	virtual size_t read(void* buff, size_t len) = 0;
	virtual size_t size() = 0;

	virtual ~CoreFile() { }
};

struct CoreFileLocal: CoreFile
{
	FILE* f = nullptr;

	static CoreFile* open(const char* path)
	{
		FILE* f = fopen(path, "rb");

		if (!f)
			return nullptr;

		CoreFileLocal* rv = new CoreFileLocal();

		rv->f = f;

		return rv;
	}

	size_t read(void* buff, size_t len)
	{
		return fread(buff,1,len,f);
	}

	void seek()
	{
		fseek(f, seek_ptr, SEEK_SET);
	}

	size_t size()
	{
		size_t p = ftell(f);
		fseek(f, 0, SEEK_END);

		size_t rv = ftell(f);
		fseek(f, p, SEEK_SET);

		return rv;
	}

	~CoreFileLocal() { fclose(f); }
};

extern "C" core_file* core_fopen(const char* filename)
{
	CoreFile* rv = nullptr;

	rv = CoreFileLocal::open(filename);

	if (rv)
	{
		core_fseek((core_file*)rv, 0, SEEK_SET);
		return (core_file*)rv;
	}
	else
	{
		printf("CoreIO: Failed to open %s\n", filename);
		return nullptr;
	}
}

extern "C" size_t core_fseek(core_file* fc, size_t offs, size_t origin) {
	CoreFile* f = (CoreFile*)fc;
	
	if (origin == SEEK_SET)
		f->seek_ptr = offs;
	else if (origin == SEEK_CUR)
		f->seek_ptr += offs;
	else
		printf("Invalid code path");

	f->seek();

	return 0;
}

extern "C" size_t core_ftell(core_file* fc) {
	CoreFile* f = (CoreFile*)fc;
	return f->seek_ptr;
}

extern "C" int core_fread(core_file* fc, void* buff, size_t len)
{
	CoreFile* f = (CoreFile*)fc;

	auto rv = f->read(buff, len);

	f->seek_ptr += rv;

	return (int)rv;
}

extern "C" int core_fclose(core_file* fc)
{
	CoreFile* f = (CoreFile*)fc;

	delete f;

	return 0;
}

extern "C" size_t core_fsize(core_file* fc)
{
	CoreFile* f = (CoreFile*)fc;

    return f->size();
}
