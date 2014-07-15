rawti-tools
===========

Tools for dealing with Texas Instruments DaVinci .rawTI files

rawtiDNG
========

Converts .rawTI files to Adobe DNG raw format.

Usage:
    ./rawtiDNG rawTI_file_in dng_file_out

Based on makeDNG from the Field project (http://openendedgroup.com/field)
with mods to read the rawTI header format. CFA pattern must currently be
edited in the source file rawtiDNG.c

Build instructions:

 * Use standard "configure", "make", "sudo make install" commands in the JPEG
   and TIFF folders jpeg-8b and tiff-3.9.4. Follow that up with ldconfig.
 * Use command "./build.sh" in the dng folder to build "rawtiDNG" executable.
