#ifndef VM_SECTIONS_H
#define VM_SECTIONS_H

/*
 * Codigo del juego que vive en la memoria virtual (GSYS VM, 0x7FC00000).
 * Los desarrolladores compilaron estos modulos con el codigo en una seccion propia; los
 * datos chicos (sdata/sbss) siguen en la memoria del cargador.
 */
#pragma section code_type ".vmtext"
#pragma section data_type ".vmdata" ".vmbss"

#endif
