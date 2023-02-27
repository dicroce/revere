r_storage

- A Revere storage file is made up of a bunch of blocks of the same size.
- These blocks are fairly large (50mb)
- The first block in the file is special because it contains a global header, and also the "dumbdex".
- The "dumbdex" is the dumb index. It is dumb because its simply a sorted array. Inserts are handled by shifting the entire array after the insertion point (hence, its dumb). This works because 1) the array isn't that big and 2) computers are pretty good at doing stuff like this now.
- Actually, the Dumbdex is two arrays the "freedex", which isn't sorted and the "dumbdex" which is sorted.
- The freedex is an array of uint16_t. The value of these entries is the index number of the storage file block (remember, storage files are made of blocks). Since the blocks are all the same size you can compute the location in the file of the block by its index by multiplying its index number times the block size.
- The dumbdex is an array of entries that contain a uint64_t and a uint16_t. The uint64_t is the timestamp of the video contained in the block, the uint16_t is the index of the block in question.
- When a file is first allocated the freedex is full and the dumbdex is empty.
- Since the dumbdex is sorted and consists of fixed size entries it is binary search able.

- SO, what all this structure accomplishes is to get queries to the correct block in O(log N). Once you have a block number, then what?

- Brief background knowledge side track: video frames come in a few types (I, P, B). Only I frames are independently decodable. Decoding a particular P requires decoding the preceeding I frame and all of the frames between.

- Because of this, the r_storage now has a layer optimized for storing "Independent Blocks" called r_ind_block. Within the payload of an ind block is another layer called r_rel_block. Ultimately, all data winds up in r_rel_blocks IN r_ind_blocks.
For video, r_rel_block provides access to the frames within a GOP. For audio, r_rel_block is still used even though the frames generally do not have the intra dependence.

- Let's back up a bit. r_ind_block generally represents a few minutes of data from a camera. It also needs to store multiple streams (video & audio).

- BUT, since we write whole GOP's inside r_rel_block's we need a way to hold the current gop + whatever audio frames arrived during this gop in memory until we have accumulated a complete gop and can write it all out.

- This is implemented in r_storage_file using the GOP buffer.

- Since r_rel_block is generally storing at most a GOP it is designed to allow accumulation of frame data into a contiguous in memory buffer... it can also be created from a buffer. This allows r_storage_file to accumulate arriving frames directly into a r_rel_block and then it can write the whole r_rel_block into the r_ind_block payload with a single memcpy once it detects that it has a complete gop.