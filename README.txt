This is repo contains the following; 

baseport/
	The baseport (BSP) needed to run Symbian OS in QEMU

docs/
	Various documentation about the baseport, peripherals and how to use QEMU.
	The wiki folders contains a dump from Wiki documentation, this the unfortunate .doc format.

symbian-qemu-0.9.1-12/
	A version of QEMU with some extra features not yet available in the QEMU mainline.
	A corresponding set of Windows binaries is available from the Symbian Foundation 
  wiki: see http://developer.symbian.org/wiki/index.php/SYBORG/QEMU

Some notes;

* The source layout for baseport now matches the Symbian Foundation layout, and 
  will compile correctly if this repository is unpacked to /sf/os/QEMU. Eventually
  the baseport could move into the kernelhwsrv or boardsupport packages, but that
  would imply changing AsspNKernIncludePath in baseport/syborg/variant.mmh
  
* Even though a version of QEMU is supplied, the TOT version of QEMU should be usable 
  (but you will have to build it yourself). The QEMU version is supplied here to 
  share some the extra features not yet available in mainline QEMU. Most noticeable 
  the support for Python peripherals (and binary separation of QEMU and its peripheral) 
  and the skinning support to supply a landing zone for phone specific buttons etc.

* Throughout the documentation the SVP is referenced. SVP stands for the 
  "Symbian Virtual Platform" and this was the proposed product name of the 
  QEMU based simulator. For the purpose of this document treat the SVP as 
  QEMU with the syborg board models.
