# Original overlay state and native texture sampling

Build181 passes the full85-suite regression. Focused original CPU overlay
state tests pass15,157 checks, immediate sampler contracts25,357 and native
expansion contracts32,771. These checks do not prove the full overlay caller
completes on actual game input. Original caller827F58D8 (1136 bytes) saves
eleven RenderWare settings, enables target0 expanded blending, changes those
settings, calls827BCAB8, disables expansion and restores the saved settings.
The caller and original data remain immutable. This boundary is state-setting;
it does not establish a rendered overlay, menu or gameplay milestone.

The original824025A8 dispatcher now admits CPU-only selectors7,20,29 after a
read-only preflight. Their source tables are shade82062CA0[0,1,2],
cull82062D70[0,0,2,6], and compare82062DA4[0,0,1,2,3,4,5,6,7].
Their original request caches, converted pending values and dirty queues remain
in AOT code. Shade queues195, cull38 and compare68. Compare also retains
the original value!=8 flag and conditionally calls the alpha-test queue helper60
when the pending blend-enable flag is nonzero. The helper suppresses an equal
pending value; the inline queues suppress only an equal retained RW request.
Preflight checks table identity, bounds, selected dirty membership, total
capacity and the complete stack/write extent before the original prologue.

Selectors2 and9 have immediate sampling effects. The dispatcher remains AOT;
its two calls enter a native transaction that invokes the original helpers
82401660 and824017A0 in a checked CPU callback frame. AddressUV uses the
original five-word conversion table and independently suppresses matching U/V
requests. Filter uses the original fourteen pairs, resets signed anisotropy
requests greater than one, writes minification then magnification, and changes
mip filtering only when its separate raw cache differs. Every CPU store,
conversion, branch and helper return remains in the original AOT bodies.

Only six SDK write sites are replaced. They update the retained native sampler
owner immediately, after checking the expected CPU store, call order, owner,
thread, context, frame and argument values. The entire resulting native state
is validated before any original CPU mutation. On a callback failure, native
state, engine caches and the inner callback stack roll back together. Direct
entry to these helpers or SDK callbacks outside the transaction rejects.
No console device object, GPU command stream, shader compilation, texture
allocation or draw success is fabricated by these state operations.

RW request caches and directly applied application state are separate original
domains. A retained-request equality therefore preserves the original skip,
even if an application setter has independently changed native-effective state.
The later draw must still validate that effective state against its own
qualified texture and shader profile. Accepting a CPU shade/cull/compare request
is not permission to draw an otherwise unsupported rendering path.

Native effective-state validation currently admits baseline point/linear
minification and magnification. Original anisotropic and other unimplemented
filter conversions fail before the original helper mutates caches. Address
metadata can retain the original values, while each actual draw's native
sampler policy imposes its separately verified restrictions.
