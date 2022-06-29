# Lab 1

It is to think to use an array to hold the input. The array size is
the capacity `_capacity`. So in the code, I think it is better to
use a pointer `_start` to indicate the current window start address.
So logically, `_start` is mapped to the array index 0. However, we need
a way to know whether the array index is accessed. So maybe hash is
a good idea, however, I think here I can only need an array called
`dirty` to indicate whether the array index is accessed. Simple idea.

However, the implementation is a little trivial. There are many details
need to handle. You can see the code. I write so many comments. And it's
easy for you to understand.
