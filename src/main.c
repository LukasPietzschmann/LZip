#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned code;
	unsigned bit_length;
} tree_node;

typedef struct huffman_node {
	int code;
	struct huffman_node* lhs;
	struct huffman_node* rhs;
} huffman_node;

typedef struct {
	unsigned end;
	unsigned bit_length;
} huffman_range;

// see RFC1951 (https://www.rfc-editor.org/rfc/rfc1951)
void build_huffman_tree(
	huffman_node* root, unsigned numof_ranges, huffman_range* ranges) {
	// Determine the maximal bit-length (they are probably unordered)
	unsigned max_bit_length = 0;
	for(unsigned i = 0; i < numof_ranges; ++i) {
		if(ranges[i].bit_length > max_bit_length)
			max_bit_length = ranges[i].bit_length;
	}

	unsigned* numof_codes_per_length = malloc(sizeof(unsigned) * (max_bit_length + 1));
	unsigned* next_code = malloc(sizeof(unsigned) * (max_bit_length + 1));
	tree_node* tree = malloc(sizeof(tree_node) * (ranges[numof_ranges - 1].end + 1));

	// Determine the number of codes per bit-length
	memset(numof_codes_per_length, '\0', sizeof(unsigned) * (max_bit_length + 1));
	for(unsigned i = 0; i < numof_ranges; ++i) {
		numof_codes_per_length[ranges[i].bit_length] +=
			ranges[i].end - ((i > 0) ? (int)ranges[i - 1].end : -1);
	}

	// Figure out what the first code for each bit-length is
	memset(next_code, '\0', sizeof(unsigned) * (max_bit_length + 1));
	unsigned bits = 1;
	unsigned code = 0;
	for(; bits <= max_bit_length; ++bits) {
		code = (code + numof_codes_per_length[bits - 1]) << 1;
		if(numof_codes_per_length[bits])
			next_code[bits] = code;
	}

	// Assign a code for each symbol from every range
	memset(tree, '\0', sizeof(tree_node) * (ranges[numof_ranges - 1].end + 1));
	unsigned active_range = 0;
	for(unsigned i = 0; i <= ranges[numof_ranges - 1].end; ++i) {
		if(i > ranges[active_range].end)
			++active_range;
		if(ranges[active_range].bit_length) {
			tree[i].bit_length = ranges[active_range].bit_length;

			if(tree[i].bit_length != 0) {
				tree[i].code = next_code[tree[i].bit_length];
				++next_code[tree[i].bit_length];
			}
		}
	}

	// Transform code table into a Huffman tree
	root->code = -1;
	for(unsigned i = 0; i <= ranges[numof_ranges - 1].end; ++i) {
		huffman_node* node = root;
		if(tree[i].bit_length) {
			for(bits = tree[i].bit_length; bits; --bits) {
				if(tree[i].code & (1 << (bits - 1))) {
					if(!node->rhs) {
						node->rhs = malloc(sizeof(huffman_node));
						memset(node->rhs, '\0', sizeof(huffman_node));
						node->rhs->code = -1;
					}
					node = (huffman_node*)node->rhs;
				} else {
					if(!node->lhs) {
						node->lhs = malloc(sizeof(huffman_node));
						memset(node->lhs, '\0', sizeof(huffman_node));
						node->lhs->code = -1;
					}
					node = (huffman_node*)node->lhs;
				}
			}
			assert(node->code == -1);
			node->code = i;
		}
	}

	free(numof_codes_per_length);
	free(next_code);
	free(tree);
}

/**
 * Build a Huffman tree for the following values:
 *   0 - 143: 00110000  - 10111111     (8)
 * 144 - 255: 110010000 - 111111111    (9)
 * 256 - 279: 0000000   - 0010111      (7)
 * 280 - 287: 11000000  - 11000111     (8)
 * See RFC 1951 rules in section 3.2.2
 * This is used to (de)compress small inputs.
 */
void build_fixed_huffman_tree(huffman_node* root) {
	huffman_range range[4] = {{143, 8}, {255, 9}, {279, 7}, {287, 8}};
	build_huffman_tree(root, 4, range);
}

typedef struct {
	FILE* source;
	unsigned char buf;
	// current bit position within buffer
	// 8 is MSB
	unsigned char mask;
} bit_stream;

// Read a single bit from the stream
unsigned next_bit(bit_stream* stream) {
	unsigned bit = 0;

	bit = (stream->buf & stream->mask) ? 1 : 0;
	// gzip's bit-orderung is absolutely fucked!
	// bytes should be read sequentially,  interpreting the bits within them is done
	// right-to-left, but then reversed for interpretation???
	stream->mask <<= 1;

	if(!stream->mask) {
		stream->mask = 1;
		if(fread(&stream->buf, 1, 1, stream->source) < 1)
			perror("Error reading compressed input");
	}

	return bit;
}

// Read multiple bits from the stream
unsigned read_bits(bit_stream* stream, unsigned numof_bits) {
	unsigned bits_value = 0;

	while(numof_bits--)
		bits_value = (bits_value << 1) | next_bit(stream);

	return bits_value;
}

unsigned read_bits_and_invert(bit_stream* stream, unsigned numof_bits) {
	unsigned bits_value = 0;

	for(unsigned i = 0; i < numof_bits; ++i)
		bits_value |= (next_bit(stream) << i);

	return bits_value;
}

// Build a Huffman tree from input (see 3.2.7)
void read_dynamic_huffman_tree(
	bit_stream* stream, huffman_node* literals_root, huffman_node* distances_root) {
	unsigned i, j;

	unsigned code_length_offsets[] = {
		16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

	unsigned hlit = read_bits_and_invert(stream, 5);
	unsigned hdist = read_bits_and_invert(stream, 5);
	unsigned hclen = read_bits_and_invert(stream, 4);

	unsigned code_lengths[19];
	huffman_range code_length_ranges[19];
	memset(code_lengths, '\0', sizeof(code_lengths));
	for(i = 0; i < (hclen + 4); ++i)
		code_lengths[code_length_offsets[i]] = read_bits_and_invert(stream, 3);

	j = 0;
	for(i = 0; i < 19; ++i) {
		if((i > 0) && (code_lengths[i] != code_lengths[i - 1]))
			++j;
		code_length_ranges[j].end = i;
		code_length_ranges[j].bit_length = code_lengths[i];
	}

	huffman_node code_lengths_root;
	memset(&code_lengths_root, '\0', sizeof(huffman_node));
	build_huffman_tree(&code_lengths_root, j + 1, code_length_ranges);

	// Read the literal/length alphabet
	// This is encoded using the Huffman tree from the previous step
	i = 0;
	int* alphabet = malloc((hlit + hdist + 258) * sizeof(int));
	huffman_range* alphabet_ranges =
		malloc((hlit + hdist + 258) * sizeof(huffman_range));
	huffman_node* code_lengths_node = &code_lengths_root;
	while(i < (hlit + hdist + 258)) {
		if(next_bit(stream))
			code_lengths_node = code_lengths_node->rhs;
		else
			code_lengths_node = code_lengths_node->lhs;

		if(code_lengths_node->code != -1) {
			if(code_lengths_node->code > 15) {
				unsigned repeat_length;

				switch(code_lengths_node->code) {
					case 16: repeat_length = read_bits_and_invert(stream, 2) + 3; break;
					case 17: repeat_length = read_bits_and_invert(stream, 3) + 3; break;
					case 18:
						repeat_length = read_bits_and_invert(stream, 7) + 11;
						break;
				}

				while(repeat_length--) {
					if(code_lengths_node->code == 16)
						alphabet[i] = alphabet[i - 1];
					else
						alphabet[i] = 0;
					++i;
				}
			} else {
				alphabet[i] = code_lengths_node->code;
				++i;
			}

			code_lengths_node = &code_lengths_root;
		}
	}

	// Turn alphabet lengths into a valid range declaration and build the final Huffman
	// code from it
	j = 0;
	for(i = 0; i <= (hlit + 257); ++i) {
		if((i > 0) && (alphabet[i] != alphabet[i - 1]))
			++j;
		alphabet_ranges[j].end = i;
		alphabet_ranges[j].bit_length = alphabet[i];
	}

	build_huffman_tree(literals_root, j, alphabet_ranges);

	--i;
	j = 0;
	for(; i <= (hdist + hlit + 258); ++i) {
		if((i > (257 + hlit)) && (alphabet[i] != alphabet[i - 1]))
			++j;
		alphabet_ranges[j].end = i - (257 + hlit);
		alphabet_ranges[j].bit_length = alphabet[i];
	}

	build_huffman_tree(distances_root, j, alphabet_ranges);

	free(alphabet);
	free(alphabet_ranges);
}
enum { MAX_BUF = 255 };
// Read a null-terminated string from a file
// Null terminated strings in files suck
bool read_string(FILE* in, char** target) {
	char buffer[MAX_BUF];
	char* buf_ptr;

	buf_ptr = buffer;

	// TODO deal with strings > MAX_BUF
	do {
		if(fread(buf_ptr, 1, 1, in) < 1) {
			perror("Error reading string value");
			return false;
		}
	} while(*(buf_ptr++));

	*target = malloc(buf_ptr - buffer);
	strncpy(*target, buffer, buf_ptr - buffer);

	return true;
}

