
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include <stdint.h>
#include <stdio.h>

#include <mach/vm_map.h>
#include <mach/mach.h>

#include <string.h>

#include "symbol_internal.h"

void get_syms_in_single_image(mach_header_t *header, uintptr_t *syms, char **strs, size_t *nsyms) {
  segment_command_t *cur_seg_cmd;
  segment_command_t *linkedit_segment   = NULL;
  segment_command_t *data_segment       = NULL;
  segment_command_t *text_segment       = NULL;
  struct symtab_command *symtab_cmd     = NULL;
  struct dysymtab_command *dysymtab_cmd = NULL;

  uintptr_t cur = (uintptr_t)header + sizeof(mach_header_t);
  for (int i = 0; i < header->ncmds; i++, cur += cur_seg_cmd->cmdsize) {
    cur_seg_cmd = (segment_command_t *)cur;
    if (cur_seg_cmd->cmd == LC_SEGMENT_ARCH_DEPENDENT) {
      if (strcmp(cur_seg_cmd->segname, "__LINKEDIT") == 0) {
        linkedit_segment = cur_seg_cmd;
      } else if (strcmp(cur_seg_cmd->segname, "__DATA") == 0) {
        data_segment = cur_seg_cmd;
      } else if (strcmp(cur_seg_cmd->segname, "__TEXT") == 0) {
        text_segment = cur_seg_cmd;
      }
    } else if (cur_seg_cmd->cmd == LC_SYMTAB) {
      symtab_cmd = (struct symtab_command *)cur_seg_cmd;
    } else if (cur_seg_cmd->cmd == LC_DYSYMTAB) {
      dysymtab_cmd = (struct dysymtab_command *)cur_seg_cmd;
    }
  }

  if (!symtab_cmd || !linkedit_segment) {
    return NULL;
  }

  uintptr_t slide         = (uintptr_t)header - (uintptr_t)text_segment->vmaddr;
  uintptr_t linkedit_base = (uintptr_t)slide + linkedit_segment->vmaddr - linkedit_segment->fileoff;
  nlist_t *symtab         = (nlist_t *)(linkedit_base + symtab_cmd->symoff);
  char *strtab            = (char *)(linkedit_base + symtab_cmd->stroff);
  size_t symtab_count     = symtab_cmd->nsyms;

  *nsyms = symtab_count;
  *syms  = symtab;
  *strs  = strtab;
}

void *iterateSymbolTable(char *name, nlist_t *syms, size_t nsyms, char *strs) {
  for (uint32_t i = 0; i < nsyms; i++) {
    if (syms[i].n_value) {
      uint32_t strtab_offset = syms[i].n_un.n_strx;
      char *tmp_symbol_name  = strs + strtab_offset;
      // TODO: what you want !!!
      if (strcmp(tmp_symbol_name, name) == 0) {
        return (void *)(syms[i].n_value);
      }
    }
  }
  return NULL;
}

void *DobbyFindSymbol(const char *image_name, const char *symbol_name) {
  void *result    = NULL;
  int image_count = _dyld_image_count();

  for (size_t i = 0; i < image_count; i++) {
    const struct mach_header *header = _dyld_get_image_header(i);
    uintptr_t slide                  = _dyld_get_image_vmaddr_slide(i);
    const char *name_                = _dyld_get_image_name(i);
    name_                            = strrchr(name_, '/') + 1;
    if (image_name != NULL && strcmp(image_name, name_))
      continue;

    size_t nsyms  = 0;
    nlist_t *syms = 0;
    char *strs    = 0;

    if (is_addr_in_dyld_shared_cache(header, 0))
      get_syms_in_dyld_shared_cache(header, &syms, &strs, &nsyms);
    else
      get_syms_in_single_image(header, &syms, &strs, &nsyms);

    result = iterateSymbolTable(symbol_name, syms, nsyms, strs);
    result = (uintptr_t)result + slide;
    if (result)
      break;
  }

  return result;
}
