int main() { return 0; }
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
