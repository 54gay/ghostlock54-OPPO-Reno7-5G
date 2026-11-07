#include "modpost.h"
void handle_moddevtable(struct module *mod, struct elf_info *info,
			Elf_Sym *sym, const char *symname)
{
	(void)mod; (void)info; (void)sym; (void)symname;
}
void add_moddevtable(struct buffer *buf, struct module *mod)
{
	(void)buf; (void)mod;
}
