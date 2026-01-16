obj-m += vtfs.o

PWD := $(CURDIR)

vtfs-objs := super.o vtfs_file.o vtfs_inode.o vtfs_dir.o

all:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	$(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean