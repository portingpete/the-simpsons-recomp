"""Generate fused temporal exports around the audited original world shaders.
The original Draw PS still produces color/depth and performs its exact discard.
The original VS body is reused with a separate previous transform/bone/morph bank.
This source generator launches no compiler or GPU process.
"""
from pathlib import Path
import argparse
import json
import re

PROFILES=json.loads((Path(__file__).with_name("temporal_geometry_profiles.json")).read_text(encoding="utf-8"))

def expand(root, source, shader_dir, seen=None):
    seen=set() if seen is None else seen
    path=root/source
    if source.startswith("build/shaders/"):path=shader_dir/Path(source).name
    path=path.resolve()
    if path in seen:return ""
    seen.add(path)
    text=path.read_text(encoding="utf-8")
    def include(match):
        name=match.group(1)
        for candidate in (path.parent/name,shader_dir/name,root/"renderer"/name):
            if candidate.is_file():
                try:relative=str(candidate.relative_to(root)).replace("\\","/")
                except ValueError:relative=str(candidate)
                return expand(root,relative,shader_dir,seen)
        raise ValueError(f"Missing audited shader include {name} from {source}")
    return re.sub(r'^\s*#include\s+"([^"]+)"[^\n]*$',include,text,flags=re.M)

def function(text,name):
    match=re.search(r"\b(\w+)\s+"+re.escape(name)+r"\s*\(\s*(\w+)\s+(\w+)\s*(?::\s*\w+)?\s*\)\s*(?::[^{]+)?\{",text)
    if not match:raise ValueError(f"Missing audited entry {name}")
    start=match.start();pos=match.end();depth=1
    while depth:
        if pos>=len(text):raise ValueError(f"Unclosed audited entry {name}")
        if text[pos]=="{":depth+=1
        if text[pos]=="}":depth-=1
        pos+=1
    return text[start:pos],match.group(1),match.group(2),match.group(3)

def emit(root,profile,shader_dir):
    address,pixel,source,entry,pixel_entry,kind=profile
    text=expand(root,source,shader_dir)
    # The packed wrappers are plain single-sample entries; FXC and source
    # analysis see the same body and destination load semantics.
    analysis=text
    occupied={int(n) for n in re.findall(r"register\s*\(\s*b(\d+)\s*\)",text)}
    if occupied.intersection({10,11,12}):raise ValueError(f"Temporal constant slots collide with original shader {source}")
    body,return_type,input_type,input_name=function(text,entry)
    bank="immediateConstants" if kind=="Immediate" else "ballC" if kind=="Ball" else "vc" if re.search(r"\bvc\s*\[",body) else "c"
    previous=re.sub(r"\b"+entry+r"\b","TemporalPrevious",body)
    previous=re.sub(r"\b"+bank+r"\b","previousConstants",previous)
    previous=re.sub(r"\bmonoBooleans\b","previousBooleans",previous)
    pixel_body,pixel_type,varying_type,_=function(analysis,pixel_entry)
    structs={m.group(1):m.group(2) for m in re.finditer(r"\bstruct\s+(\w+)\s*\{([^}]*)\}\s*;",text,flags=re.S)}
    varying=structs[varying_type]
    color_field=re.search(r"\b((?:float|uint|int)[1-4]?)\s+color\s*:",structs[pixel_type])
    depth_field=re.search(r"\b((?:float|uint|int)[1-4]?)\s+depth\s*:",structs[pixel_type])
    if not color_field or not depth_field:raise ValueError(f"Original Draw PS output type missing: {pixel_entry}")
    color_type=color_field.group(1);depth_type=depth_field.group(1)
    fields=re.findall(r"\b(\w+)\s*:\s*(?:SV_|TEXCOORD|COLOR)[^;]*;",varying)
    if "position" not in fields:raise ValueError(f"Missing original position export in {entry}")
    current_call=entry+"("+input_name+")"
    input_parameters=input_type+" "+input_name
    previous_call="TemporalPrevious("+input_name+")"
    previous_setup=""
    if kind=="Ball":
        input_parameters+=" : TEXCOORD0, uint temporalVertexId : SV_VertexID"
        previous_call="TemporalPrevious(previousConstants[9+temporalVertexId])"
    if kind=="Immediate":
        if input_type!="ImmediateInput" or bank!="immediateConstants":
            raise ValueError("Temporal Immediate requires the original mode-zero CPU declaration")
        input_parameters+=", uint temporalVertexId : SV_VertexID"
        previous_setup=("    ImmediateInput previousInput="+input_name+";\n"
                        "    previousInput.position=previousConstants[5+temporalVertexId].xyz;\n")
        previous_call="TemporalPrevious(previousInput)"
    if varying_type=="SkinDrawVarying":
        current_call="TemporalSkinDraw("+input_name+")"
        text+="\nSkinDrawVarying TemporalSkinDraw(SkinInput input) {\nSkinOutput original=VSSkin(input);SkinDrawVarying result;\nresult.position=original.position;result.t0=original.t0;result.t1=original.t1;result.t2=original.t2;result.t3=original.t3;result.rimControl=original.t3.x;return result;\n}\n"
    if re.search(r"TEXCOORD(?:14|15)\b",varying):raise ValueError("Temporal semantics collide with original exports")
    transfer="\n".join(f"    result.{field}=original.{field};" for field in fields)
    restore="\n".join(f"    original.{field}=input.{field};" for field in fields)
    motion_lines="""    bool valid=historyValid!=0 && input.currentClip.w>0 && input.previousClip.w>0;
    result.motion=valid?(input.previousClip.xy/input.previousClip.w-input.currentClip.xy/input.currentClip.w)*float2(0.5,-0.5)*renderSize:float2(0,0);
"""
    if kind=="Immediate":
        motion_lines="""    bool valid=historyValid!=0 && input.currentClip.w>0 && input.previousClip.w>0
        && all(isfinite(input.currentClip)) && all(isfinite(input.previousClip));
    result.motion=float2(0,0);
    if(valid) {
        precise float2 current=input.currentClip.xy/input.currentClip.w;
        precise float2 previous=input.previousClip.xy/input.previousClip.w;
        precise float2 motion=(previous-current)*float2(0.5,-0.5)*renderSize;
        valid=all(isfinite(current)) && all(isfinite(previous)) && all(isfinite(motion));
        if(valid)result.motion=motion;
    }
"""
    return f"""// Fused exact color/depth/coverage: VS{address}, PS{pixel}.
{text}
#include "temporal_motion_common.hlsl"
cbuffer TemporalPreviousConstants : register(b10) {{ float4 previousConstants[256]; }};
cbuffer TemporalPreviousBooleans : register(b12) {{ uint4 previousBooleans; }};
{previous}
struct TemporalFusedVarying {{ {varying}
    float4 currentClip : TEXCOORD14;
    float4 previousClip : TEXCOORD15;
}};
TemporalFusedVarying VSTemporal_{address}({input_parameters}) {{
    {varying_type} original={current_call};
    TemporalFusedVarying result;
{transfer}
    result.currentClip=original.position;
{previous_setup}    result.previousClip={previous_call}.position;
    result.position.xy+=float2(2.0*jitterPixels.x/renderSize.x,-2.0*jitterPixels.y/renderSize.y)*result.position.w;
    return result;
}}
struct TemporalFusedOutput {{
    {color_type} color : SV_Target0;
    float2 motion : SV_Target1;
    float biasCurrent : SV_Target2;
    {depth_type} depth : SV_Depth;
}};
TemporalFusedOutput PSTemporal_{address}(TemporalFusedVarying input) {{
    {varying_type} original;
{restore}
    {pixel_type} shaded={pixel_entry}(original);
    TemporalFusedOutput result;
    result.color=shaded.color;result.depth=shaded.depth;
{motion_lines}    result.biasCurrent=valid&&conservativeCurrent==0?0.0:1.0;
    return result;
}}
"""

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--shader-dir",type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    shader_dir=args.shader_dir or root/"build/shaders"
    args.output.mkdir(parents=True,exist_ok=True)
    for profile in PROFILES:
        content=emit(root,profile,shader_dir)
        dest=args.output/f"temporal_{profile[0]}.hlsl"
        if not dest.exists() or dest.read_text(encoding="utf-8")!=content:
            dest.write_text(content,encoding="utf-8",newline="\n")
    (args.output/"temporal_geometry.profiles").write_text("\n".join(" ".join(p) for p in PROFILES)+"\n",encoding="utf-8")

if __name__=="__main__":main()
