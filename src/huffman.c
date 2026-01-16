/* Canonical Huffman coding */

#include "huffman.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    huffman_node_t **nodes;
    size_t size;
    size_t capacity;
} pqueue_t;

static void pqueue_init(pqueue_t *pq, size_t capacity) {
    pq->nodes = malloc(capacity * sizeof(huffman_node_t *));
    pq->size = 0;
    pq->capacity = capacity;
}

static void pqueue_free(pqueue_t *pq) { free(pq->nodes); }

static void pqueue_push(pqueue_t *pq, huffman_node_t *node) {
    size_t i = pq->size++;
    pq->nodes[i] = node;
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (pq->nodes[parent]->freq <= pq->nodes[i]->freq) break;
        huffman_node_t *tmp = pq->nodes[parent];
        pq->nodes[parent] = pq->nodes[i];
        pq->nodes[i] = tmp;
        i = parent;
    }
}

static huffman_node_t *pqueue_pop(pqueue_t *pq) {
    if (pq->size == 0) return NULL;
    huffman_node_t *min = pq->nodes[0];
    pq->nodes[0] = pq->nodes[--pq->size];
    size_t i = 0;
    while (1) {
        size_t left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left < pq->size && pq->nodes[left]->freq < pq->nodes[smallest]->freq)
            smallest = left;
        if (right < pq->size && pq->nodes[right]->freq < pq->nodes[smallest]->freq)
            smallest = right;
        if (smallest == i) break;
        huffman_node_t *tmp = pq->nodes[i];
        pq->nodes[i] = pq->nodes[smallest];
        pq->nodes[smallest] = tmp;
        i = smallest;
    }
    return min;
}

static void compute_lengths(huffman_node_t *node, uint8_t depth, uint8_t *lengths) {
    if (!node) return;
    if (node->symbol >= 0) {
        lengths[node->symbol] = (depth > HUFFMAN_MAX_CODE_LEN) ? HUFFMAN_MAX_CODE_LEN : depth;
    } else {
        compute_lengths(node->left, depth + 1, lengths);
        compute_lengths(node->right, depth + 1, lengths);
    }
}

static void free_tree(huffman_node_t *node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    free(node);
}

void huffman_build_codes(const uint32_t *freq, huffman_code_t *codes) {
    uint8_t lengths[256] = {0};
    
    size_t num_symbols = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) num_symbols++;
    }
    
    if (num_symbols == 0) {
        memset(codes, 0, 256 * sizeof(huffman_code_t));
        return;
    }
    
    if (num_symbols == 1) {
        for (int i = 0; i < 256; i++) {
            codes[i].code = 0;
            codes[i].length = (freq[i] > 0) ? 1 : 0;
        }
        return;
    }
    
    /* Build tree */
    pqueue_t pq;
    pqueue_init(&pq, 512);
    
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            huffman_node_t *node = malloc(sizeof(huffman_node_t));
            node->freq = freq[i];
            node->symbol = i;
            node->left = node->right = NULL;
            pqueue_push(&pq, node);
        }
    }
    
    while (pq.size > 1) {
        huffman_node_t *left = pqueue_pop(&pq);
        huffman_node_t *right = pqueue_pop(&pq);
        huffman_node_t *parent = malloc(sizeof(huffman_node_t));
        parent->freq = left->freq + right->freq;
        parent->symbol = -1;
        parent->left = left;
        parent->right = right;
        pqueue_push(&pq, parent);
    }
    
    huffman_node_t *root = pqueue_pop(&pq);
    pqueue_free(&pq);
    compute_lengths(root, 0, lengths);
    free_tree(root);
    
    /* Build canonical codes from lengths */
    uint32_t bl_count[HUFFMAN_MAX_CODE_LEN + 1] = {0};
    for (int i = 0; i < 256; i++) {
        if (lengths[i] > 0) bl_count[lengths[i]]++;
    }
    
    uint32_t next_code[HUFFMAN_MAX_CODE_LEN + 1] = {0};
    uint32_t code = 0;
    for (int bits = 1; bits <= HUFFMAN_MAX_CODE_LEN; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    
    for (int i = 0; i < 256; i++) {
        if (lengths[i] > 0) {
            codes[i].code = next_code[lengths[i]]++;
            codes[i].length = lengths[i];
        } else {
            codes[i].code = 0;
            codes[i].length = 0;
        }
    }
}

uint8_t huffman_encode(const uint8_t *input, size_t input_size,
                       const huffman_code_t *codes,
                       uint8_t *output, size_t *output_size) {
    size_t out_idx = 0;
    uint64_t buffer = 0;
    int bits_in_buffer = 0;
    
    for (size_t i = 0; i < input_size; i++) {
        uint8_t sym = input[i];
        uint32_t code = codes[sym].code;
        uint8_t len = codes[sym].length;
        
        /* Add code to buffer */
        buffer = (buffer << len) | code;
        bits_in_buffer += len;
        
        /* Output complete bytes */
        while (bits_in_buffer >= 8) {
            bits_in_buffer -= 8;
            output[out_idx++] = (buffer >> bits_in_buffer) & 0xFF;
        }
    }
    
    /* Flush remaining bits */
    uint8_t padding = 0;
    if (bits_in_buffer > 0) {
        padding = 8 - bits_in_buffer;
        output[out_idx++] = (buffer << padding) & 0xFF;
    }
    
    *output_size = out_idx;
    return padding;
}

void huffman_decode(const uint8_t *input, size_t input_size,
                    const uint8_t *code_lengths,
                    uint8_t *output, size_t output_size,
                    uint8_t padding_bits) {
    huffman_code_t codes[256];
    
    uint32_t bl_count[HUFFMAN_MAX_CODE_LEN + 1] = {0};
    for (int i = 0; i < 256; i++) {
        if (code_lengths[i] > 0) bl_count[code_lengths[i]]++;
    }
    
    uint32_t next_code[HUFFMAN_MAX_CODE_LEN + 1] = {0};
    uint32_t code = 0;
    for (int bits = 1; bits <= HUFFMAN_MAX_CODE_LEN; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    
    for (int i = 0; i < 256; i++) {
        if (code_lengths[i] > 0) {
            codes[i].code = next_code[code_lengths[i]]++;
            codes[i].length = code_lengths[i];
        } else {
            codes[i].length = 0;
        }
    }
    
    size_t out_idx = 0;
    size_t bit_pos = 0;
    size_t total_bits = input_size * 8 - padding_bits;
    
    while (out_idx < output_size && bit_pos < total_bits) {
        uint32_t current_code = 0;
        uint8_t current_len = 0;
        
        while (current_len < HUFFMAN_MAX_CODE_LEN && bit_pos < total_bits) {
            size_t byte_idx = bit_pos / 8;
            int bit_idx = 7 - (bit_pos % 8);
            int bit = (input[byte_idx] >> bit_idx) & 1;
            current_code = (current_code << 1) | bit;
            current_len++;
            bit_pos++;
            
            for (int sym = 0; sym < 256; sym++) {
                if (codes[sym].length == current_len && codes[sym].code == current_code) {
                    output[out_idx++] = sym;
                    goto next_symbol;
                }
            }
        }
        break;
        next_symbol:;
    }
}
