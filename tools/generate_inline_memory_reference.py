"""Copy verified original bodies for tests using external memory calls."""
import argparse
import json
from pathlib import Path
from inline_fast_memory_generation import original_body, body_digest, code_only, MEMORY, BYTE_ENTRY, PAIR_ENTRY, VM_ENTRY

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--matrix-source',type=Path,required=True)
p.add_argument('--runtime-source',type=Path,required=True)
p.add_argument('--vector-source',type=Path,required=True)
p.add_argument('--blockcopy-source',type=Path,required=True)
p.add_argument('--bytecopy-source',type=Path,required=True)
p.add_argument('--pair-source',type=Path,required=True)
p.add_argument('--vm-source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
def body(path,symbol,reference):
    source=path.read_text(encoding="utf-8")
    marker=f'PPC_FUNC_IMPL({symbol}) {{'
    if source.count(marker)!=1:raise ValueError(f'Expected exactly one original {symbol} body')
    start=source.index(marker)
    end=source.index('\n}\n',start)+3
    entry=symbol.removeprefix('__imp__sub_')
    original=original_body(source[start:end],entry)
    if entry in (BYTE_ENTRY,PAIR_ENTRY,VM_ENTRY,'82B2A1C0','82A3D1D8'):
        selections=json.loads((Path(__file__).resolve().parents[1]/'config/inline_fast_memory.json').read_text())
        selected=[row for row in selections if row['entry']==entry]
        if len(selected)!=1 or body_digest(original.removesuffix('\n'))!=selected[0]['body_sha256']:
            raise ValueError(f'Selected {entry} reference original body changed; requalify selection')
    return original.replace(marker,f'PPC_FUNC({reference}) {{',1)
# The test reference needs C++ linkage: /EHsc otherwise lets callers assume an
# extern-C reference cannot throw, removing the catch around deliberate faults.
# Production is called through its existing C++-declared sub_ alias.
output='// Test only: exact original bodies with distinct C++-linkage reference symbols.\n'
output+=body(a.source,'__imp__sub_823EBD00','referenceMatrix')
output+='\n'+body(a.matrix_source,'__imp__sub_823F2D98','referenceMatrixCompose')
for symbol,reference in [('__imp__sub_82A3CD80','referenceCopy'),
                         ('__imp____savegprlr_28','referenceSave28'),
                         ('__imp____restgprlr_28','referenceRestore28')]:
    output+='\n'+body(a.runtime_source,symbol,reference)
output+='\n'+body(a.vector_source,'__imp__sub_82B74110','referenceVectorCopy')
output+='\n'+body(a.blockcopy_source,'__imp__sub_8270A590','referenceWordBlockCopy')
for path,symbol,reference in ((a.matrix_source,'__imp__sub_823F2D98','referenceFastMatrixCompose'),
                               (a.blockcopy_source,'__imp__sub_8270A590','referenceFastWordBlockCopy'),
                               (a.bytecopy_source,'__imp__sub_8281E9B0','referenceFastByteDecode'),
                               (a.pair_source,'__imp__sub_82B2A8D0','referenceFastPairShift'),
                               (a.vm_source,'__imp__sub_82844AB0','referenceFastVMSetup'),
                               (a.pair_source,'__imp__sub_82B2A1C0','referenceFastPairBitset'),
                               (a.runtime_source,'__imp__sub_82A3D1D8','referenceFastScalarCopy')):
    text=body(path,symbol,reference)
    for match in reversed(list(MEMORY.finditer(code_only(text)))):
        replacement={'PPC_LOAD_U32':'PPC_EXTERNAL_FAST_LOAD_U32',
                     'PPC_STORE_U32':'PPC_EXTERNAL_FAST_STORE_U32',
                     'PPC_LOAD_U8':'PPC_EXTERNAL_FAST_LOAD_U8',
                     'PPC_STORE_U8':'PPC_EXTERNAL_FAST_STORE_U8',
                     'PPC_LOAD_U16':'PPC_EXTERNAL_FAST_LOAD_U16',
                     'PPC_STORE_U16':'PPC_EXTERNAL_FAST_STORE_U16'}[match.group()]
        text=text[:match.start()]+replacement+text[match.end():]
    if symbol=='__imp__sub_82B2A1C0':
        # Keep both original 16-byte vector expressions on the production
        # external hardware-fast route, scoped only to this new reference.
        text=('#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY\n'
              '#pragma push_macro("PPCGuestPointer")\n#undef PPCGuestPointer\n'
              '#define PPCGuestPointer PPCGuestPointerFast\n#endif\n'+text+
              '\n#if defined(SIMPSONS_FAST_GUEST_MEMORY) && SIMPSONS_FAST_GUEST_MEMORY\n'
              '#pragma pop_macro("PPCGuestPointer")\n#endif\n')
    output+='\n'+text
if not a.output.exists() or a.output.read_text(encoding="utf-8")!=output:a.output.write_text(output)
