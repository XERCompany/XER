import os, sys

# (dir, [(fn, id, name, [(pname,min,max,def),...]), ...])
PLUGINS = {
"xer_fx_adjust": [
  ("exposure","xer.fx.exposure","Exposure",[("stops",-4,4,0)]),
  ("gamma_corr","xer.fx.gamma","Gamma",[("gamma",0.2,4,1)]),
  ("levels","xer.fx.levels","Levels",[("black",0,128,0),("white",128,255,255),("mid_gamma",0.2,4,1)]),
  ("auto_contrast","xer.fx.auto_contrast","Auto Contrast",[("clip_pct",0,10,0.5)]),
  ("temperature","xer.fx.temperature","Temperature",[("amount",-100,100,0)]),
  ("tint","xer.fx.tint","Tint",[("amount",-100,100,0)]),
  ("vibrance","xer.fx.vibrance","Vibrance",[("amount",-100,100,0)]),
  ("lift_gamma_gain","xer.fx.lift_gamma_gain","Lift/Gamma/Gain",[("lift",-1,1,0),("gamma",0.2,4,1),("gain",0,4,1)]),
],
"xer_fx_color": [
  ("hue_rotate","xer.fx.hue_rotate","Hue Rotate",[("degrees",-180,180,0)]),
  ("sepia_tone","xer.fx.sepia","Sepia",[("amount",0,1,1)]),
  ("duotone","xer.fx.duotone","Duotone",[("shadow_hue",0,360,220),("light_hue",0,360,40)]),
  ("channel_mixer","xer.fx.channel_mixer","Channel Mixer",[("r_gain",0,2,1),("g_gain",0,2,1),("b_gain",0,2,1)]),
  ("invert_lum","xer.fx.invert_lum","Invert Luma",[("amount",0,1,1)]),
  ("color_isolate","xer.fx.color_isolate","Color Isolate",[("target_hue",0,360,0),("tolerance",1,90,20)]),
  ("rgb_swap","xer.fx.rgb_swap","RGB Swap",[("order",0,5,0)]),
  ("saturation_boost","xer.fx.saturation_boost","Saturation Boost",[("amount",-100,100,30)]),
],
"xer_fx_blur": [
  ("directional_blur","xer.fx.directional_blur","Directional Blur",[("radius",1,64,12),("angle",0,360,0)]),
  ("radial_blur","xer.fx.radial_blur","Radial Blur",[("strength",0,1,0.3),("cx",0,1,0.5),("cy",0,1,0.5)]),
  ("zoom_blur","xer.fx.zoom_blur","Zoom Blur",[("strength",0,1,0.3)]),
  ("fast_gaussian","xer.fx.fast_gaussian","Fast Gaussian",[("radius",1,32,6)]),
  ("unsharp_mask","xer.fx.unsharp_mask","Unsharp Mask",[("amount",0,4,1),("radius",1,16,3)]),
  ("median3","xer.fx.median3","Median 3x3",[("passes",1,4,1)]),
],
"xer_fx_style": [
  ("solarize","xer.fx.solarize","Solarize",[("threshold",0,255,128)]),
  ("threshold_bw","xer.fx.threshold_bw","Threshold B/W",[("level",0,255,128)]),
  ("scanlines","xer.fx.scanlines","Scanlines",[("spacing",2,16,4),("strength",0,1,0.5)]),
  ("neon_edges","xer.fx.neon_edges","Neon Edges",[("threshold",0,255,40),("hue",0,360,300)]),
  ("halftone","xer.fx.halftone","Halftone",[("cell",4,32,8)]),
  ("emboss_color","xer.fx.emboss_color","Emboss Color",[("angle",0,360,135),("strength",0,2,1)]),
  ("edge_glow","xer.fx.edge_glow","Edge Glow",[("threshold",0,255,40),("intensity",0,4,1.5)]),
  ("tone_split","xer.fx.tone_split","Tone Split",[("shadow_hue",0,360,220),("highlight_hue",0,360,40)]),
],
"xer_fx_distort": [
  ("wave_h","xer.fx.wave_h","Wave Horizontal",[("amplitude",0,64,12),("wavelength",8,512,120)]),
  ("wave_v","xer.fx.wave_v","Wave Vertical",[("amplitude",0,64,12),("wavelength",8,512,120)]),
  ("ripple","xer.fx.ripple","Ripple",[("amplitude",0,32,10),("wavelength",8,256,60)]),
  ("mirror_quad","xer.fx.mirror","Mirror",[("mode",0,3,0)]),
  ("tile","xer.fx.tile","Tile",[("count",2,8,3)]),
  ("twirl","xer.fx.twirl","Twirl",[("angle",-360,360,90)]),
],
"xer_fx_overlay": [
  ("film_grain","xer.fx.film_grain","Film Grain",[("amount",0,100,20),("mono",0,1,1)]),
  ("letterbox","xer.fx.letterbox","Letterbox",[("fraction",0,0.45,0.12)]),
  ("vignette_soft","xer.fx.vignette_soft","Vignette Soft",[("strength",0,1,0.5),("radius",0,1,0.7)]),
  ("grid_lines","xer.fx.grid_lines","Grid Lines",[("cells",2,16,4),("alpha",0,1,0.3)]),
  ("border_frame","xer.fx.border_frame","Border Frame",[("thickness",0,64,8)]),
  ("corner_marks","xer.fx.corner_marks","Corner Marks",[("length",4,64,24)]),
],
"xer_audio_fx": [
  ("echo","xer.af.echo","Echo",[("delay_ms",1,2000,350),("feedback",0,0.95,0.35),("mix",0,1,0.4)]),
  ("tremolo","xer.af.tremolo","Tremolo",[("rate",0.1,20,5),("depth",0,1,0.6)]),
  ("autopan","xer.af.autopan","Auto Pan",[("rate",0.1,20,1),("depth",0,1,0.8)]),
  ("bitcrusher","xer.af.bitcrusher","Bitcrusher",[("bits",1,16,8),("downsample",1,64,4)]),
  ("onepole_lp","xer.af.onepole_lp","Lowpass",[("cutoff",100,18000,4000)]),
  ("soft_clip","xer.af.soft_clip","Soft Clip",[("drive",1,20,4)]),
],
}

AUDIO = "xer_audio_fx"
for d, effects in PLUGINS.items():
    is_audio = d == AUDIO
    kind = "XER_PLUGIN_AUDIO_FX" if is_audio else "XER_PLUGIN_VIDEO_FX"
    lines = []
    lines.append("// ABI table — binds effects.h prototypes to the plugin")
    lines.append("// exports. Param descriptors feed the inspector UI.")
    lines.append('#include "../common/entry.h"')
    lines.append('#include "effects.h"')
    lines.append("")
    for i,(fn,fid,name,params) in enumerate(effects):
        if params:
            lines.append(f"static const XER_ParamDesc kP{i}[] = {{")
            for (pn,lo,hi,dv) in params:
                lines.append(f'    {{"{pn}", {lo}f, {hi}f, {dv}f}},')
            lines.append("};")
    lines.append("")
    lines.append("static const plugin::Entry kEntries[] = {")
    for i,(fn,fid,name,params) in enumerate(effects):
        pfn = f"kP{i}" if params else "nullptr"
        n = len(params)
        vfn = f"fx::{fn}" if not is_audio else "nullptr"
        afn = f"fx::{fn}" if is_audio else "nullptr"
        lines.append(f'    {{"{fid}", "{name}", {kind}, {vfn}, {afn}, {pfn}, {n}}},')
    lines.append("};")
    lines.append("")
    lines.append("static const int32_t kCount =")
    lines.append("    static_cast<int32_t>(sizeof(kEntries) / sizeof(kEntries[0]));")
    lines.append("")
    lines.append('#include "../common/exports_impl.h"')
    open(os.path.join(d,"exports.cc"),"w",newline="\n").write("\n".join(lines)+"\n")
    print("wrote", d+"/exports.cc", len(effects), "effects")
