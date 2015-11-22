## malloc_libary

Dynamic Memory Allocation Library

A dynamic storage allocator in C.

The library consists of four functions, described below.  The semantics of these functions matches the
semantics of the corresponding malloc, realloc, and free routines in libc. Type man malloc to the
shell for complete documentation.

#Library Functions

mm init: Before calling mm malloc, mm realloc or mm free, the application program (i.e., the trace-
driven driver program that you will use to evaluate your implementation) calls mm init to perform any
necessary initializations, such as allocating the initial heap area. The return value should be -1 if there was a
problem in performing the initialization, 0 otherwise.

mm malloc: The mm malloc routine returns a pointer to an allocated block payload of at least size bytes.
The entire allocated block should lie within the heap region and should not overlap with any other allocated
chunk. We will be comparing your implementation to the version of malloc supplied in the standard C
library (libc). Since the libc malloc always returns payload pointers that are aligned to 16 bytes on the
x86 64 architecture, so your malloc implementation should do likewise and always return 16-byte aligned
pointers.

mm free: The mm free routine frees the block pointed to by ptr. It returns nothing. This routine is
only guaranteed to work when the passed pointer (ptr) was returned by an earlier call to mm malloc or
mm realloc and has not yet been freed.

mm realloc: The mm realloc routine returns a pointer to an allocated region of at least size bytes with
the following constraints.
– if ptr is NULL, the call is equivalent to mm malloc(size);
– if size is equal to zero, the call is equivalent to mm free(ptr);
– if ptr is not NULL,it must have been returned by an earlier call to mm malloc or mm realloc. The
call to mm realloc changes the size of the memory block pointed to by ptr (the old block) to size
bytes and returns the address of the new block. Notice that the address of the new block might be the
same as the old block, or it might be different, depending on your implementation, the amount of internal
fragmentation in the old block, and the size of the realloc request.
The contents of the new block are the same as those of the old ptr block, up to the minimum of the old
and new sizes. Everything else is uninitialized. For example, if the old block is 16 bytes and the new
block is 24 bytes, then the first 16 bytes of the new block are identical to the first 16 bytes of the old
block and the last 8 bytes are uninitialized. Similarly, if the old block is 24 bytes and the new block is
16 bytes, then the contents of the new block are identical to the first 16 bytes of the old block.

