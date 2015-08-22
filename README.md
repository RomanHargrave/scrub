# scrub.c

`scrub` is a utility that behaves like `rm -r`, except that rather than
simply removing everything it will only remove what you tell it to.

In summary, it works as follows:

``` 
    For every file, e, in a directory
        If the file is not a directory 
            Remove the file if:
                the file's name is in the list of file names to clobber
                or 
                the file's extension is in the list of extensions to clobber 
        Otherwise, do the above for the directory 
```

I wrote this to handle importing media in to my collection. A lot of stuff often 
includes `md5sum` files which are useless after I change file metadata, and the
utilities I use to manage these collections aren't that good at cleaning up
after themselves.

After I import something, I often have a structure like this left over

```
    downloads/
        empty_folder/
        oddball_folder/
            album.nfo
            album.md5sums
        art/
            important.png
```

If I were to simply run `rm -r`, I would loose `important.png`.
If I were to use `find` to remove empty folders, I would still be left with 
`oddball_folder/`. In order to clean this up as much as possible, I can run
`scrub -cnfo -cmd5sums downloads` on the folder, which will leave the following 
structure:

```
    downloads/
        art/
            important.png 
```

Additionally, if `scrub` cannot completely remove _all_ files and directories it is
passed, it will return `ENOTEMPTY` or similar, so that it may be used in automation
scripts.

# Compiling

`scrub.c` has no dependencies other than on a C99 or better standard library and a
POSIX-compliant system. This probably does not work on Windows, but I do not care
and will not bother finding out as the C library and compiler available on Windows 
it absolute trash from what I understand.

To compile it, run `gcc` or your compiler of choice on `scrub.c`.

Function attributes like `hot` are included and will be inserted by the preprocessor
if it detects `__GNUC__` (defined by GCC).
