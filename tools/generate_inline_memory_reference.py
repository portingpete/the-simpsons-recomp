"""Copy verified original bodies for tests using external memory calls."""
import argparse
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
p.add_argument('--matrix-source',type=Path,required=True)
p.add_argument('--runtime-source',type=Path,required=True)
p.add_argument('--vector-source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
def body(path,symbol,reference):
    source=path.read_text(encoding="utf-8")
    marker=f'PPC_FUNC_IMPL({symbol}) {{'
    if source.count(marker)!=1:raise ValueError(f'Expected exactly one original {symbol} body')
    start=source.index(marker)
    end=source.index('\n}\n',start)+3
    return source[start:end].replace(marker,f'PPC_FUNC({reference}) {{',1)
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
if not a.output.exists() or a.output.read_text(encoding="utf-8")!=output:a.output.write_text(output)
