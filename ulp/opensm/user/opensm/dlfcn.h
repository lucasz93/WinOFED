

#define RTLD_LAZY 1
 
void *dlopen(char *lib, int flags) { return (void*)0; }

void *dlsym(void *hdl, char *sym)
{
	return (void*)0; // always fail.
}

char * dlerror(void) { return NULL; }

void dlclose(void *hdl) { }
