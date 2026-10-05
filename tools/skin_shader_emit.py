"""Offline ALU emission shared by the pinned skin vertex shader records."""
import analyze_rigid_shader as rigid
from analyze_screen_shaders import require


def indexed_bones(text, row):
    f = row['fields']
    if not any(f[k] for k in ('constant_address_register_relative',
                              'constant_0_relative', 'constant_1_relative')):
        return text
    # All qualified skin bone reads have one constant operand, in src1.
    # The constant selector counts constant operands, not ALU source positions.
    used = f['sources'][:2] if f['vector_opcode'] == 1 else f['sources']
    constants = [s for s in used if s['bank'] == 'constant']
    require(f['constant_address_register_relative'] and f['constant_0_relative'] and
            not f['constant_1_relative'] and len(constants) == 1 and
            constants[0] == f['sources'][1] and constants[0]['register'] in (52, 53, 54),
            'Unqualified skin constant addressing')
    reg = constants[0]['register']
    require(f'vc[{reg}]' in text, 'Missing indexed skin bone expression')
    return text.replace(f'vc[{reg}]', f'vc[a0+{reg}]')


def vs_issue(slot, row):
    f = row['fields']
    require(not row['fetch'] and not f['predicated'] and
            not f['vector_destination_relative'] and
            not f['scalar_destination_relative_or_export_zero'] and
            not f['absolute_constants'], 'Unqualified skin VS modifiers')
    if f['scalar_opcode'] == 23:
        source = f['sources'][2]
        require(source['bank'] == 'temporary' and not source['relative_temporary'] and
                not source['absolute_temporary'] and not source['negated'] and
                len(set(source['components'])) == 1 and not f['scalar_mask'] and
                not f['scalar_clamp'], 'Unqualified skin MaxAs')
        operand = rigid.operand(f, 2, 'VS', 1)
        # Co-issued vector reads the old address. Compute both scalar results
        # before publishing any vector destination, then advance a0 afterwards.
        vector = {**row, 'fields': {**f, 'scalar_opcode': 50}}
        body = indexed_bones(rigid.issue(slot, vector, 'VS'), vector)
        if f['vector_mask'] and f['vector_opcode'] == 1:
            a, b = [rigid.operand(f, i, 'VS') for i in range(2)]
            body = body.replace(indexed_bones(a+'*'+b, row),
                                indexed_bones('rigidLegacyMultiply('+a+','+b+')', row))
        return ('    { // MaxAs slot%d\n' % slot +
                f'        precise float nextScalar={operand};\n' +
                f'        int nextAddress=int(clamp(floor({operand}+0.5),-256.0,255.0));\n' +
                body + '\n        ps=nextScalar; a0=nextAddress;\n    }')
    text = rigid.issue(slot, row, 'VS')
    if f['vector_mask'] and f['vector_opcode'] == 1:
        a, b = [rigid.operand(f, i, 'VS') for i in range(2)]
        text = text.replace(a+'*'+b, 'rigidLegacyMultiply('+a+','+b+')')
    return indexed_bones(text, row)
