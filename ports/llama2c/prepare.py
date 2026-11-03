"""Extract pinned MIT neural/tokenizer code; keep the upstream source unchanged."""
from pathlib import Path

def prepare(root:Path,target:Path):
    source=(root/'vendor/llama2c/run.c').read_text(encoding='utf-8')
    structs=source[source.index('typedef struct {'):source.index('void malloc_run_state(')]
    structs=structs.replace('ssize_t file_size','long file_size')
    weights=source[source.index('void memory_map_weights('):source.index('void read_checkpoint(')]
    forward=source[source.index('void rmsnorm('):source.index('void build_tokenizer(')]
    encode=source[source.index('int str_lookup('):source.index('// The Sampler,')]
    encode=encode.replace('bsearch(', 'ai_bsearch(')
    encode=encode.replace('t->sorted_vocab = malloc(t->vocab_size * sizeof(TokenIndex));',
        't->sorted_vocab = malloc(t->vocab_size * sizeof(TokenIndex));\n'
        '        if (!t->sorted_vocab) { *n_tokens=0; return; }')
    line='char* str_buffer = malloc((t->max_token_length*2 +1 +2) * sizeof(char));'
    if line not in encode:raise RuntimeError('Pinned tokenizer allocation changed')
    encode=encode.replace(line,line+'\n    if (!str_buffer) { *n_tokens=0; return; }')
    output=target/'ai-upstream.h'
    output.write_text('/* Derived from karpathy/llama2.c; MIT, see vendor/llama2c/LICENSE. */\n'+
        structs+weights+forward+encode,encoding='utf-8')
    return output
