Blue Coat File System
=====================

You may remember our `last post <https://insinuator.net/2016/12/research-diary-blue-coat/>`_
regarding the SGOS system and the proprietary file system. Since then, we got access to a newer
version of the system (6.6.4.2). Still not the most current one (which seems to be 6.7.1.1) nor 
of the 6.6.x branch (which seems to be 6.6.5.1) though. As this system version also used the same 
proprietary filesystem (although it initially booted from a FAT32 partition), I decided to take 
a deeper look into this.

By comparing the different images and with the information of previous researchers [1]_ I was 
able to reconstruct (a part of) the filesystem and develop a fuse based driver.

As already described in the last post, the filesystem primarily is built upon multiple headers
with two magic fields:

.. code-block:: c

    struct cp_xx_entry {
        union {
            char c[4];
            u_int32_t i;
        } magic1;

        int32_t unknown;
        u_int16_t size;
        u_int16_t unknown2;

        union {
            char c[4];
            u_int32_t i;
        } magic2;
    };


The current magic values I identified during the analysis are:

.. list-table::
    :header-rows: 1

    * - Magic 1
      - Magic 2
      - Possible Name
    * - _CP_
      - _HP_
      - Partition
    * - _CP_
      - _CZK
      - SubPartition
    * - _CP_
      - _CE_
      - Element
    * - _CP_
      - _VE_
      - Config
    * - _CP_
      - _IE_
      - File
    * - _CP_
      - BCWZ
      - Boot
    * - _CP_
      - RHDP
      - PartitionTable
    * - _CP_
      - YEDP
      - PartitionTableInfo
    * - _CP_
      - EEDP
      - PartitionTableEntry

For a complete list of the specific items, please take a look at `the bcfs.c file<https://github.com/ernw/bcfs-fuse/blob/master/bcfs.c>`_.

Whilst the old version of the firmware is using this filesystem over the complete disk, 
the new version consists of a FAT32 filesystem with the following file structure:

.. raw:: html

    <pre>
    sgos
    └── boot
        ├── cmpnts
        │   ├── boot.exe
        │   └── starter.si
        ├── meta.txt
        └── systems
            └── system1
    </pre>

The files ``starter.si`` and ``system1`` are actual images containing the proprietary filesystem,
starting with a ``Partition``-Header.

This is how this header looks like (first as normal hexdump, then decoded in radare2):

.. code-block:: hexdump

    00000000  5f 43 50 5f 88 00 00 00  00 0c 00 00 5f 48 50 5f  |_CP_........_HP_|
    00000010  e3 f1 33 99 74 66 1f e3  df 7c 0c f4 1b a8 9a 0b  |..3.tf...|......|
    00000020  66 ba 85 a7 b8 05 8f ef  0d 36 07 cb 00 00 00 00  |f........6......|
    00000030  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
    *
    00000050  00 00 00 00 00 00 00 00  0d 48 ba bb 9e 24 2a fd  |.........H...$*.|
    00000060  6f 4d dc 19 1c 0c c6 3f  fd 40 0d 33 00 00 00 00  |oM.....?.@.3....|

::

    [0x00000000]> pf.PartitionHeader [4]cdww[4]cdd[3048]b magic1 hdr_unknown size hdr_unknown2 magic2 header_checksum data_checksum unknown
    [0x00000000]> pf.PartitionHeader 
             magic1 : 0x00000000 = [ '_', 'C', 'P', '_' ]
        hdr_unknown : 0x00000004 = 136
               size : 0x00000008 = 0x0c00
       hdr_unknown2 : 0x0000000a = 0x0000
             magic2 : 0x0000000c = [ '_', 'H', 'P', '_' ]
    header_checksum : 0x00000010 = 2570318307
      data_checksum : 0x00000014 = 3810485876
            unknown : 0x00000018 = [ 0xdf, 0x7c, 0x0c, 0xf4, 0x1b,... ]

This header is immediately followed by a ``SubPartition``-Header. This header has the following format:

.. code-block:: c

    struct cp_ce_entry {
        struct cp_xx_entry hdr;
        int32_t num_elements;
        int32_t unknown2;
        u_int32_t offset;
        int32_t unknown3[9];
    };

    struct cp_czk_entry {
        struct cp_xx_entry hdr;
        char unknown[0xc0];
        struct cp_ce_entry entries[16];
        char unknown2[0x34];
        /* signature */
        char pkcs7[0x706+0x27f5];
    };

Besides some still unknown fields, it consists of a list of ``Element`` entries and a signature.
This signature seems to be used as a secure boot mechanism and is verified by the loader of the 
particular filesystem.

The element entries are specifying the location of different parts of the filesystem (as
describted by Raphaël Rigo [1]_). The offsets are based on the start of the "partition".

The first entry points to a global string table which contains all strings used in the 
filesystem (filenames, informations?). This table consists of length/offset pairs as shown below:

.. code-block:: hexdump

    00004000  00 00 00 00 00 00 00 00  a0 27 00 00 00 00 00 00  |.........'......|
    00004010  04 00 00 00 00 00 00 00  a8 27 00 00 00 00 00 00  |.........'......|
    00004020  08 00 00 00 00 00 00 00  b0 27 00 00 00 00 00 00  |.........'......|
    00004030  6d 00 00 00 00 00 00 00  c0 27 00 00 00 00 00 00  |m........'......|
    00004040  14 00 00 00 00 00 00 00  30 28 00 00 00 00 00 00  |........0(......|
    00004050  86 00 00 00 00 00 00 00  48 28 00 00 00 00 00 00  |........H(......|
    00004060  0a 00 00 00 00 00 00 00  d0 28 00 00 00 00 00 00  |.........(......|

::

    [0x00004000]> pf 7qq size offset
    0x00004000 [0] {
        size : 0x00004000 = (qword)0x0000000000000000
      offset : 0x00004008 = (qword)0x00000000000027a0
    }
    0x00004010 [1] {
        size : 0x00004010 = (qword)0x0000000000000004
      offset : 0x00004018 = (qword)0x00000000000027a8
    }
    0x00004020 [2] {
        size : 0x00004020 = (qword)0x0000000000000008
      offset : 0x00004028 = (qword)0x00000000000027b0
    }
    0x00004030 [3] {
        size : 0x00004030 = (qword)0x000000000000006d
      offset : 0x00004038 = (qword)0x00000000000027c0
    }
    0x00004040 [4] {
        size : 0x00004040 = (qword)0x0000000000000014
      offset : 0x00004048 = (qword)0x0000000000002830
    }
    0x00004050 [5] {
        size : 0x00004050 = (qword)0x0000000000000086
      offset : 0x00004058 = (qword)0x0000000000002848
    }
    0x00004060 [6] {
        size : 0x00004060 = (qword)0x000000000000000a
      offset : 0x00004068 = (qword)0x00000000000028d0
    }

The offsets are this time based on the start of the table instead of the partition:

.. code-block:: hexdump

    000067a0  00 00 00 00 00 00 00 00  53 47 4f 53 00 00 00 00  |........SGOS....|
    000067b0  53 63 6f 72 70 69 75 73  00 00 00 00 00 00 00 00  |Scorpius........|
    000067c0  2f 57 6f 72 6b 73 70 61  63 65 73 2f 6a 65 6e 6b  |/Workspaces/jenk|
    000067d0  69 6e 73 2f 77 6f 72 6b  73 70 61 63 65 2f 53 47  |ins/workspace/SG|
    000067e0  4f 53 36 5f 73 67 5f 36  5f 36 5f 78 78 33 2f 73  |OS6_sg_6_6_xx3/s|
    000067f0  63 6f 72 70 69 75 73 2f  73 67 5f 36 5f 36 5f 78  |corpius/sg_6_6_x|
    00006800  78 33 2f 62 6f 6f 74 63  68 61 69 6e 2f 78 38 36  |x3/bootchain/x86|

In the second section, the filesystem options are stored. For a detailed description, take
a look at Rigo's talk (slide 12) [1]_.

The third sections points to a list of files stored in this filesystem. Those files
are represented by the ``File``-Header:

.. code-block:: c

    struct cp_ie_entry {
        struct cp_xx_entry hdr;
        u_int64_t offset;
        u_int64_t size;
        u_int32_t path_idx;
        u_int32_t filename_idx;
        u_uint8_t unknown[256-44];
    };

The offsets are (again) based on the start of the section. The index fields are refering to
the previously described string table. For example for the first entry:

::

          magic1 : 0x00015000 = [ '_', 'C', 'P', '_' ]
     hdr_unknown : 0x00015004 = 0x00000070
            size : 0x00015008 = 0x0100
    hdr_unknown2 : 0x0001500a = 0x0000
          magic2 : 0x0001500c = [ '_', 'I', 'E', '_' ]
          offset : 0x00015010 = (qword)0x0000000000014000
        filesize : 0x00015018 = (qword)0x0000000000007e1c
         path_id : 0x00015020 = 0x00000030
     filename_id : 0x00015024 = 0x00000000

Filename @ index 0::

    (emptystring)

Path @ index 0x30::

    /Workspaces/jenkins/workspace/SGOS6_sg_6_6_xx3/scorpius/sg_6_6_xx3/bootchain/x86/release/x86_64_prekernel.exe

With this information, it was actually possible to write a fuse filesystem driver to mount those
images:

.. code-block:: bash

    $ ./bcfs_fuse --imagefile=./sgos/boot/cmpnts/starter.si /tmp/starter.si
    Mounting imagefile ./sgos/boot/cmpnts/starter.si
    $ tree /tmp/starter.si
    /tmp/starter.si
    ├── main.cfg
    ├── var
    │   └── lib
    │       └── jenkins
    │           └── workspace
    │               └── SGOS6_scorpius_main
    │                   └── scorpius
    │                       └── toolchain
    │                           └── linux
    │                               └── x86_64_host
    │                                   └── gcc-cross
    │                                       └── x86_target
    │                                           └── v4.4.2
    │                                               └── i386-bcsi-sgos
    │                                                   └── lib
    │                                                       ├── libgcc_s_sgos.so
    │                                                       └── libstdc++_sgos.so
    └── Workspaces
        └── jenkins
            └── workspace
                └── SGOS6_scorpius_main
                    └── scorpius
                        └── main
                            └── bin
                                └── x86
                                    └── sgos_native
                                        └── release
                                            └── gcc_v4.4.2
                                                └── stripped
                                                    ├── boot
                                                    │   └── kernel.exe
                                                    ├── console_rdr.exe
                                                    ├── libbooting_crypto.so
                                                    ├── libchar_output.so
                                                    ├── libc.so
                                                    ├── libgcc_support.so
                                                    ├── libknl_api.so
                                                    ├── libmemory.so
                                                    ├── libm.so
                                                    ├── libosd_api.so
                                                    ├── osd.exe
                                                    ├── sequencer.exe
                                                    ├── starter.exe
                                                    ├── storage
                                                    │   ├── aic79xx.exe
                                                    │   ├── ata.exe
                                                    │   ├── libadmin.exe.so
                                                    │   ├── mpt.exe
                                                    │   ├── scsi.exe
                                                    │   └── vioblk.exe
                                                    └── sysimg_partition.exe


As told before, the filesystem is protected by a secure boot mechanism. This means that the fuse
driver is currently read only. Nevertheless, the 5.x version of the firmware does not use a signature.
But based on the work of Rigo and my research, a HMAC is still used to secure the integrity. As
long as the key for the HMAC is not available (which was not by simply analysing the data structures)
the stored data couldn't be modified.

The source code of the (readonly) fuse driver is published together with this blogpost at github.

Best,

Timo

.. [1] https://www.blackhat.com/docs/eu-15/materials/eu-15-Rigo-A-Peek-Under-The-Blue-Coat.pdf
