/**
 * Instant — browser demo.
 *
 * Instant film developing in front of you. The one idea, from `source/Model.h`
 * and `AGENTS.md`: an integral print develops in the open, over minutes, and
 * the picture arrives in a known order. A Take exposes the clip's frame, the
 * camera's averaging meter sets the exposure, and then each texel carries two
 * doses — the dye's and the timing layer's — accumulated every frame from the
 * film-seconds after the reagent reached it, each weighted by its own
 * Arrhenius rate. The print pass turns them into three first-order dye layers
 * under an opacifier that clears as the timing layer drops the pH, which also
 * ends development: dark green-grey first, then pale and blue-cyan, warming.
 *
 * This plugin is TEMPORAL: its picture depends on state carried across frames
 * (the capture, two ping-ponged dose buffers, the meter) and on a clock kept in
 * double on the CPU. So the two halves of this page are not equally faithful.
 *
 *   The shaders are the plugin's. The seven GLSL strings of
 *   `source/Shaders.cpp` — the vertex body, the `kModel` library (sRGB, the
 *   PCG hash, the spread, the curve) and the capture, meter, develop, resample
 *   and print bodies — are spliced into the generated block below by
 *   `demo/tools/sync_shaders.py`, tabs and comments included, together with
 *   the constants block `constants()` writes from Model.h at run time (the
 *   same text, byte for byte). `assemble` below is Shaders.cpp's.
 *   `demo/tools/check_shaders.py --dump` compares every assembled stage with
 *   what the plugin itself compiles (`intest --dump-shaders`), and
 *   `tools/verify.sh` runs it.
 *
 *   ONE SPELLING differs, and it is the page's, not the shader's. The
 *   constants block writes Model.h's numbers with C's `%.9g`, so three
 *   integer-valued floats come out as `const float kFrontSpeed = 1;`,
 *   `kKnee = 6;` and `kMeterMaxGain = 8;`. Desktop GLSL 4.10 converts an int
 *   initialiser to float implicitly; GLSL ES 3.00 has no implicit conversions
 *   and refuses it. The page tries the plugin's exact text first on every load
 *   and reports the compiler's verdict under the picture; then it compiles
 *   with those three lines — and only lines of the exact shape
 *   `const float kName = <integer>;` — spelled `1.0`, `6.0`, `8.0`. The value
 *   of each is the same float; nothing else is touched. Said in the
 *   disclosure.
 *
 *   The CPU half is a PORT — of `Controls.cpp` (every slider to its unit, the
 *   print's layout in millimetres, the Arrhenius factor), the constants of
 *   `Model.h` (copied by the script), and `Instant::ProcessOpenGL`: the clock,
 *   the take (edge-triggered, Continuous and Take modes, the interval), the
 *   film age in double, the Arrhenius factors, the buffers and their clears,
 *   the resize resample-and-swap, the layout and the crop, the stock
 *   arithmetic (Expired, the balance) and every uniform, in their order — line
 *   for line, in JavaScript doubles as the C++ keeps them, rounded to float
 *   where the plugin hands a float over. Nothing checks a port but a reader.
 *   `intest --develop`, `--order`, `--arrhenius`, `--take`, `--meter` and the
 *   rest check the C++ originals and have no idea this page exists.
 *
 * ------------------------------------------------------------- the buffers
 *
 * The plugin's: the capture RGBA16F, two dose buffers RG32F (ping-pong), all
 * three linear-sampled, and the meter a 64 × 64 R32F with a mip chain it reads
 * at level 6. Rendering into a float texture is an extension in WebGL2
 * (EXT_color_buffer_float), and filtering one — which the print pass does to
 * the dose and the meter's mip chain needs — is another
 * (OES_texture_float_linear). The page refuses to start without either rather
 * than fall back to 8 bits.
 *
 * ------------------------------------------------------------- the clock
 *
 * The plugin reads the host's clock and votes on its unit (readout's scheme),
 * falling back to a wall clock for a host that never calls SetTime. Here the
 * clock is the kit's: `time` in declared seconds, accumulated from frame
 * deltas while playing (the kit caps one delta at 0.1 s), +1/60 on Step, 0 on
 * Restart. No vote runs. The plugin's own rules then apply unchanged: dt is
 * now − last clamped to [0, 0.25] s, a nominal 1/60 on the first frame; film
 * age grows by dt × Speed. A paused page renders only when a control moves
 * and each such frame is worth 0 s, so a paused print holds, as it does in a
 * paused host. Restart sends the clock back to 0, which the plugin handles
 * itself: "a host clock that runs backwards restarts the interval and keeps
 * the print".
 *
 * ------------------------------------------------------------- what is missing
 *
 * **Take is a button under the canvas.** It is FF_TYPE_EVENT in the plugin
 * and the kit has no event control. A press is delivered as the plugin's
 * SetFloatParameter delivers it — 1.0 then 0.0, edge-triggered on the crossing
 * of 0.5 — and consumed on the next rendered frame. **The About block is
 * absent**, as on every page in this suite. The harness-only `Perturb` and
 * `Probe` uniforms are set to what the shipped plugin sets them to: 0.
 *
 * And what every page in this suite is not: this is the plugin's shaders and a
 * port of its C++, not the plugin. No Resolume, no composition, no FFGL, and
 * GLSL ES 3.00 in a browser rather than desktop GL 4.1 core.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';

//=== BEGIN GENERATED by demo/tools/sync_shaders.py from source/ -- do not edit by hand.

// source/Shaders.cpp, verbatim. `assemble` (below the block) is Shaders.cpp's.
const K_VERSION = "#version 410 core";

// What Shaders.cpp's constants() writes from Model.h at run time, the same text.
const K_CONSTANTS = `const float kFullReach = 1.25;
const float kShortReach = 1.04;
const float kShortCorner = 0.28;
const float kExpiredReach = 0.3;
const float kFrontSpeed = 1;
const float kKnee = 6;
const float kRollerCircumference = 0.3125;
const float kRollerPhase = 0.5;
const float kRollerWidth = 0.018;
const float kRollerDepth = 0.6;
const float kOpacifierR = 1.89999998;
const float kOpacifierG = 1.54999995;
const float kOpacifierB = 1.79999995;
const float kUnreachedR = 0.0299999993;
const float kUnreachedG = 0.0359999985;
const float kUnreachedB = 0.0579999983;
const float kFrameR = 0.800000012;
const float kFrameG = 0.790000021;
const float kFrameB = 0.75;
const float kMeterTarget = 0.18;
const float kMeterMinGain = 0.25;
const float kMeterMaxGain = 8;
`;

const VERTEX_BODY = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV;
}
`;

const MODEL = `
uniform int Perturb;       //negative-control hooks; always 0 in the plugin
uniform int Spread;        //0 full, 1 short, 2 uneven
uniform float Expired;     //0..1
uniform vec2 CropScale;    //source uv = 0.5 + ( film uv - 0.5 ) * CropScale

//sRGB transfer, both ways, the IEC 61966-2-1 piecewise form.
float srgbDecode( float v )
{
	return v <= 0.04045 ? v / 12.92 : pow( ( v + 0.055 ) / 1.055, 2.4 );
}
vec3 srgbDecode( vec3 v )
{
	return vec3( srgbDecode( v.r ), srgbDecode( v.g ), srgbDecode( v.b ) );
}
float srgbEncode( float v )
{
	v = clamp( v, 0.0, 1.0 );
	return v <= 0.0031308 ? v * 12.92 : 1.055 * pow( v, 1.0 / 2.4 ) - 0.055;
}
vec3 srgbEncode( vec3 v )
{
	return vec3( srgbEncode( v.r ), srgbEncode( v.g ), srgbEncode( v.b ) );
}

//PCG output mix: exact in 32 bits, the same on every driver.
uint hashInt( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
float hashUnit( uint h )
{
	return float( h >> 8u ) * ( 1.0 / 16777216.0 );
}

//Value noise along a line, -1..1: one hashed value per cell, smoothstepped
//between. \`x\` in cells.
float lineNoise( float x, uint seed )
{
	float i  = floor( x );
	float t  = x - i;
	uint a   = seed * 65537u + 7u;
	float n0 = hashUnit( hashInt( a + uint( int( i ) + 4096 ) ) );
	float n1 = hashUnit( hashInt( a + uint( int( i ) + 4097 ) ) );
	return 2.0 * mix( n0, n1, t * t * ( 3.0 - 2.0 * t ) ) - 1.0;
}

//---------------------------------------------------------------------------
// The spread, in FILM coordinates: u across the image 0..1, s up from the
// pod edge in image heights. Everything here is a function of ( u, s ) and
// of the controls, so the develop pass (on the source raster) and the print
// pass (on the output raster) agree on it at any raster.
//---------------------------------------------------------------------------

//How far the reagent gets from the pod edge, in image heights.
float reachAt( float u )
{
	float w = 2.0 * u - 1.0;
	float r = kFullReach;
	if( Spread == 1 )
		r = kShortReach * ( 1.0 - kShortCorner * w * w );
	else if( Spread == 2 )
		r = 1.03 + 0.09 * lineNoise( u * 9.0, 11u ) - 0.06 * w * w;
	//Old paste has dried and thickened: it reaches less far, and least far
	//at the corners, where the frame's edge holds it back.
	r *= 1.0 - kExpiredReach * Expired * ( 0.55 + 0.45 * w * w );
	return r;
}

//The front's speed at u, image heights per film-second. Uneven paste runs
//in lanes.
float frontSpeedAt( float u )
{
	if( Spread == 2 )
		return kFrontSpeed * ( 1.0 + 0.25 * lineNoise( u * 5.0, 23u ) );
	return kFrontSpeed;
}

//When the front reaches ( u, s ), in film-seconds after the take.
float arrivalAt( float u, float s )
{
	if( ( Perturb & 2 ) != 0 )
		return 0.0;
	return max( s, 0.0 ) / frontSpeedAt( u );
}

//The reagent's depth, as a factor on the dye it can deliver: 1 on a full
//spread; lanes on an uneven one; thinning over the last few percent before
//the reach, where the tongue of paste runs out.
float depthAt( float u, float s, float reach )
{
	float d = 1.0;
	if( Spread == 2 )
		d -= 0.10 * ( 0.5 + 0.5 * lineNoise( u * 13.0, 37u ) );
	d *= 0.55 + 0.45 * smoothstep( reach, reach - 0.06, s );
	return d;
}

//---------------------------------------------------------------------------
// The curve. Coverage, as rebate's and wetplate's: 0 below the toe, 1 past
// the shoulder, a straight line of slope 1 / Latitude between.
//---------------------------------------------------------------------------
float softplus( float u )
{
	return max( u, 0.0 ) + log( 1.0 + exp( -kKnee * abs( u ) ) ) / kKnee;
}
float coverage( float logH, float Toe, float Latitude )
{
	return clamp( ( softplus( logH - Toe ) - softplus( logH - Toe - Latitude ) ) / Latitude, 0.0, 1.0 );
}
`;

const CAPTURE_BODY = `
uniform sampler2D InputTexture;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec4 c = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	//The film sees light, and a transparent pixel sent none.
	fragColor = vec4( srgbDecode( c.rgb ) * clamp( c.a, 0.0, 1.0 ), 1.0 );
}
`;

const METER_BODY = `
uniform sampler2D Capture;

in vec2 uv;
out vec4 fragColor;

void main()
{
	vec2 q = 0.5 + ( uv - 0.5 ) * CropScale;
	fragColor = vec4( dot( texture( Capture, q ).rgb, vec3( 0.2126, 0.7152, 0.0722 ) ), 0.0, 0.0, 1.0 );
}
`;

const DEVELOP_BODY = `
uniform sampler2D Previous;  //( dye dose, stop dose ), same raster
uniform vec2 Size;           //the raster, pixels
uniform float AgeStart;      //film-seconds since the take, at this frame's start
uniform float DAge;          //film-seconds this frame
uniform float KDye;          //the dye's Arrhenius rate, 1 at 24 degC
uniform float KStop;         //the timing layer's
uniform float StopDose;      //when development ends, stop-seconds

in vec2 uv;
out vec4 fragColor;

void main()
{
	ivec2 p   = ivec2( gl_FragCoord.xy );
	vec2 dose = texelFetch( Previous, p, 0 ).rg;

	//This texel's place on the film. The source raster is the whole clip
	//frame; the image window shows its centre, so film coordinates are the
	//inverse of the print pass's crop. Outside the crop the film goes on
	//(clamped), so a Border change mid-print never uncovers dry film.
	vec2 q = gl_FragCoord.xy / Size;
	vec2 f = 0.5 + ( q - 0.5 ) / CropScale;
	float u = clamp( f.x, 0.0, 1.0 );
	float s = max( f.y, 0.0 );

	if( s > reachAt( u ) )
	{
		fragColor = vec4( dose, 0.0, 1.0 );
		return;
	}

	//The part of this frame after the front arrived. In steady state the
	//whole frame, exactly: the branch keeps a large AgeStart's rounding out
	//of it.
	float t0      = arrivalAt( u, s );
	float overlap = AgeStart >= t0 ? DAge : clamp( AgeStart + DAge - t0, 0.0, DAge );

	//The timing layer runs on; the dye moves only until the stop, and on
	//the frame that crosses it only for the part of the frame before it.
	float dStop = KStop * overlap;
	float part  = dStop > 0.0 ? clamp( ( StopDose - dose.y ) / dStop, 0.0, 1.0 ) : 0.0;
	fragColor   = vec4( dose.x + KDye * overlap * part, dose.y + dStop, 0.0, 1.0 );
}
`;

const RESAMPLE_BODY = `
uniform sampler2D Source;
uniform vec2 HalfTexel;  //half a SOURCE texel

in vec2 uv;
out vec4 fragColor;

void main()
{
	//Half a texel in from the edge so GL_LINEAR at the boundary does not
	//take half its weight from the clamp. On a flat field this is exact.
	vec2 q = clamp( uv, HalfTexel, vec2( 1.0 ) - HalfTexel );
	fragColor = texture( Source, q );
}
`;

const PRINT_BODY = `
uniform sampler2D Dose;      //( dye dose, stop dose )
uniform sampler2D Capture;   //linear light at the take
uniform sampler2D Source;    //the host's input, for Mix and the viewfinder
uniform sampler2D Meter;     //the meter's reading, at its top mip level
uniform float MeterLevel;
uniform vec2 MaxUV;
uniform vec2 PrintMin;       //pixels
uniform vec2 PrintMax;
uniform vec2 ImageMin;
uniform vec2 ImageMax;
uniform int HasPrint;        //0: Take mode before the first take -- the viewfinder
uniform int Mono;            //one image layer, exposed by luminance
uniform float ExposureGain;  //2 ^ Exposure stops, the wheel on top of the meter
uniform float Toe;
uniform float Latitude;
uniform vec3 Dmin;           //per layer C M Y, reflection density, Expired applied
uniform vec3 Dmax;
uniform vec3 Balance;        //makes a neutral neutral at the stop at 24 degC
uniform vec3 Tau;            //per layer, dye-seconds
uniform float TauOp;         //the opacifier's, stop-seconds
uniform vec3 White;          //the pigment, linear reflectance
uniform float Roller;        //0..1
uniform float MixAmount;
uniform int Probe;           //1: write ( Dc, Dm, Dy, O_red ) raw

in vec2 uv;
out vec4 fragColor;

//The fraction of the pixel [ c - 0.5, c + 0.5 ] inside [ a, b ]: exact
//area coverage along one axis.
float span( float c, float a, float b )
{
	return clamp( min( c + 0.5, b ) - max( c - 0.5, a ), 0.0, 1.0 );
}

//The dirty roller: one mark per revolution along the spread, each a band
//across the print of width kRollerWidth, with exact pixel coverage along
//the spread. \`sigma\` is the pixel centre's distance from the pod edge in
//pixels, \`himg\` the image height in pixels. Computed in coordinates local
//to the nearest mark, so that when the circumference is a whole number of
//pixels every mark is the same arithmetic on the same numbers.
float rollerMarks( float sigma, float himg )
{
	float C  = kRollerCircumference * himg * ( ( Perturb & 16 ) != 0 ? 1.02 : 1.0 );
	float s0 = kRollerPhase * C;
	float hw = 0.5 * kRollerWidth * himg;
	float k  = floor( ( sigma - s0 ) / C + 0.5 );
	float m  = 0.0;
	for( int j = -1; j <= 1; ++j )
	{
		float kj = k + float( j );
		float c  = s0 + kj * C;
		if( kj < 0.0 || c > himg )
			continue;
		float local = ( sigma - s0 ) - kj * C;//the pixel centre, from this mark's centre
		m += span( local, -hw, hw );
	}
	return m;
}

void main()
{
	vec2 px = gl_FragCoord.xy;
	vec4 source = texture( Source, uv * MaxUV );
	if( HasPrint == 0 )
	{
		fragColor = source;
		return;
	}

	float printCov = span( px.x, PrintMin.x, PrintMax.x ) * span( px.y, PrintMin.y, PrintMax.y );
	float imageCov = span( px.x, ImageMin.x, ImageMax.x ) * span( px.y, ImageMin.y, ImageMax.y );
	vec2 isize     = ImageMax - ImageMin;
	vec2 f         = ( px - ImageMin ) / isize;
	vec2 q         = 0.5 + ( f - 0.5 ) * CropScale;
	float u        = clamp( f.x, 0.0, 1.0 );
	float s        = max( f.y, 0.0 );

	//The camera's exposure: the meter brings the window's mean to mid grey,
	//within its range; the wheel offsets it.
	float mean = textureLod( Meter, vec2( 0.5 ), MeterLevel ).r;
	float gain = clamp( kMeterTarget / max( mean, 1e-9 ), kMeterMinGain, kMeterMaxGain );
	if( ( Perturb & 128 ) != 0 )
		gain = 1.0;

	//The layers' asymptotic dye, from the light at the take.
	vec3 light = texture( Capture, q ).rgb * gain * ExposureGain;
	if( Mono != 0 )
		light = vec3( dot( light, vec3( 0.2126, 0.7152, 0.0722 ) ) );
	vec3 logH = log( max( light, vec3( 1e-12 ) ) ) / log( 10.0 );
	vec3 dinf = vec3( coverage( logH.r, Toe, Latitude ), coverage( logH.g, Toe, Latitude ), coverage( logH.b, Toe, Latitude ) );
	dinf      = Dmin + ( Dmax - Dmin ) * ( 1.0 - dinf );

	//The spread: what reached here, how deep, and the roller's marks.
	float reach   = reachAt( u );
	float reached = 1.0 - smoothstep( reach - 0.004, reach + 0.004, s );
	float depth   = depthAt( u, s, reach );
	float marks   = rollerMarks( px.y - ImageMin.y, isize.y );
	float across  = 0.55 + 0.45 * lineNoise( u * 7.0, 53u );//the dirt is not even along the roller
	depth *= 1.0 - Roller * kRollerDepth * across * marks;

	//Development so far.
	vec2 dose = texture( Dose, q ).rg;
	vec3 tau  = Tau;
	if( ( Perturb & 1 ) != 0 )
		tau = vec3( Tau.g );
	if( ( Perturb & 8 ) != 0 )
		tau.r *= 1.1;
	vec3 D = Balance * dinf * depth * ( 1.0 - exp( -dose.x / tau ) );
	vec3 O = vec3( kOpacifierR, kOpacifierG, kOpacifierB ) * exp( -dose.y / TauOp );

	if( Probe == 1 )
	{
		fragColor = vec4( D, O.r );
		return;
	}

	//Seen by reflection off the white pigment, through the dye and the
	//opacifier; where no reagent reached, the dark negative.
	vec3 image = White * exp2( -( D + O ) * 3.321928094887362 );
	image      = mix( vec3( kUnreachedR, kUnreachedG, kUnreachedB ), image, reached );

	//The frame, with a faint shadow along the window's edge.
	vec2 pmm   = ( px - PrintMin ) / max( PrintMax.y - PrintMin.y, 1.0 );//print heights
	vec3 frame = vec3( kFrameR, kFrameG, kFrameB );
	vec2 fromWindow = max( ImageMin - px, px - ImageMax );
	float edge = max( fromWindow.x, fromWindow.y ) / max( isize.y, 1.0 );
	frame *= 1.0 - 0.10 * ( 1.0 - smoothstep( 0.0, 0.008, edge ) );

	//Texture: the matte frame more than the glossy image. Value noise at a
	//fixed size on the print, so it does not scale with the raster.
	float grainX = lineNoise( pmm.x * 900.0 + 17.0 * floor( pmm.y * 900.0 ), 71u );
	frame *= 1.0 + 0.030 * grainX;
	image *= 1.0 + 0.006 * grainX;

	vec3 col = mix( frame, image, imageCov );

	//A small shine: a soft diagonal band of the room's light off the gloss.
	float band = ( pmm.x + pmm.y - 0.95 ) / 0.22;
	col += 0.012 * exp( -band * band );

	//Outside the print there is nothing: colour AND alpha 0, so the host
	//sees transparency whether it reads the output as straight or
	//premultiplied. The antialiased edge is premultiplied for the same reason.
	float a    = printCov;
	vec3 print = srgbEncode( col ) * a;
	if( MixAmount >= 1.0 )
	{
		fragColor = vec4( print, a );
		return;
	}
	fragColor = vec4( mix( source.rgb, print, MixAmount ), mix( source.a, a, MixAmount ) );
}
`;

// source/Model.h: every number of the print.
const MODEL_H = {};
MODEL_H.kGasConstant = 8.314462618;
MODEL_H.kReferenceC = 24.0;
MODEL_H.kDyeActivation = 67410.0;
MODEL_H.kStopActivation = 0.5 * MODEL_H.kDyeActivation;
MODEL_H.kTauMono = 90.0;
MODEL_H.kTauOpacifier = 45.0;
MODEL_H.kStopDose = 600.0;
MODEL_H.kFrontSpeed = 1.0;
MODEL_H.kRollerCircumference = 5.0 / 16.0;
MODEL_H.kRollerPhase = 0.5;
MODEL_H.kRollerWidth = 0.018;
MODEL_H.kRollerDepth = 0.6;
MODEL_H.kMeterTarget = 0.18;
MODEL_H.kMeterMinStop = -2.0;
MODEL_H.kMeterMaxStop = 3.0;
MODEL_H.kMeterSize = 64;
MODEL_H.kMeterLevel = 6;
MODEL_H.kKnee = 6.0;
MODEL_H.kExpiredDmaxLoss = 0.30;
MODEL_H.kExpiredCyanLoss = 0.45;
MODEL_H.kExpiredStain = 0.14;
MODEL_H.kExpiredReach = 0.30;
MODEL_H.kFullReach = 1.25;
MODEL_H.kShortReach = 1.04;
MODEL_H.kShortCorner = 0.28;
MODEL_H.kNominalFrame = 1.0 / 60.0;
MODEL_H.kMaxFrameDelta = 0.25;
MODEL_H.kTauColour = [70.0, 120.0, 200.0];
MODEL_H.kTauVintage = [90.0, 170.0, 230.0];
MODEL_H.kOpacifier = [Math.fround(1.90), Math.fround(1.55), Math.fround(1.80)];
MODEL_H.kWhiteColour = [Math.fround(0.86), Math.fround(0.86), Math.fround(0.84)];
MODEL_H.kWhiteMono = [Math.fround(0.85), Math.fround(0.85), Math.fround(0.86)];
MODEL_H.kUnreached = [Math.fround(0.030), Math.fround(0.036), Math.fround(0.058)];
MODEL_H.kFrameColour = [Math.fround(0.80), Math.fround(0.79), Math.fround(0.75)];
MODEL_H.kStocks = [
  { shoulder: -0.30, latitude: 1.20, dmin: [0.10, 0.10, 0.10], dmax: [1.65, 1.65, 1.65], mono: false },
  { shoulder: -0.25, latitude: 1.50, dmin: [0.06, 0.06, 0.06], dmax: [1.90, 1.90, 1.90], mono: true },
  { shoulder: -0.35, latitude: 1.05, dmin: [0.10, 0.16, 0.26], dmax: [1.35, 1.50, 1.55], mono: false },
];

// source/Controls.h and Controls.cpp: the option counts and lists.
const CONTROLS_H = {};
CONTROLS_H.kFilmCount = 3;
CONTROLS_H.kModeCount = 2;
CONTROLS_H.kModeTake = 0;
CONTROLS_H.kModeContinuous = 1;
CONTROLS_H.kSpreadCount = 3;
CONTROLS_H.kSpreadFull = 0;
CONTROLS_H.kSpreadShort = 1;
CONTROLS_H.kSpreadUneven = 2;
const FILM_NAMES = ['Colour', 'Black & White', 'Vintage'];
const MODE_NAMES = ['Take', 'Continuous'];
const SPREAD_NAMES = ['Full', 'Short', 'Uneven'];

//=== END GENERATED

/// Shaders.cpp's `assemble`: the version line, then (for the stages that
/// take the model) the constants block and the model library, then the body.
const assemble = (constants, body, withModel) => `${K_VERSION}\n${withModel ? constants + MODEL : ''}${body}`;

/// The page's one respelling (see the header): an integer-valued float
/// constant, `const float kName = 1;`, as `1.0`, which GLSL ES 3.00 accepts.
/// Only whole lines of exactly that shape; the value is the same float.
const esConstants = (text) => text.replace(/^(const float k\w+ = -?\d+);$/gm, '$1.0;');
const RESPELT = K_CONSTANTS.split('\n').filter((line) => /^const float k\w+ = -?\d+;$/.test(line)).map((line) => line.replace(/^const float (k\w+).*$/, '$1'));

//===========================================================================
// Controls.cpp, ported. What a host parameter means.
//
// The plugin stores every host value as a `float`; the page's sliders are
// doubles, so each is rounded through Math.fround — the same 24-bit value the
// plugin holds — before its law. The double laws widen that float, as the
// C++ does; the float laws round each step to float.
//===========================================================================

const f = Math.fround;
const clamp01f = (value) => Math.min(Math.max(f(value), 0.0), 1.0);
const lround = (value) => (value < 0 ? -Math.round(-value) : Math.round(value));

const controls = {
  /// Temperature: 4 to 36 degC, linear. 0.625 is exactly 24 degC.
  temperatureC: (value) => 4.0 + 32.0 * clamp01f(value),
  temperatureParam: (celsius) => clamp01f((celsius - 4.0) / 32.0),
  /// Expired: 0 fresh to 1 long past its date, linear.
  expired: (value) => clamp01f(value),
  /// Interval: host seconds between prints in Continuous mode, 0.5 to 128,
  /// geometric. 0.625 is exactly 16 s.
  intervalSeconds: (value) => Math.pow(2.0, 8.0 * clamp01f(value) - 1.0),
  intervalParam: (seconds) => clamp01f((Math.log2(Math.max(seconds, 0.5)) + 1.0) / 8.0),
  /// Exposure: -2 to +2 stops, linear. 0.5 is exactly 0.
  exposureStops: (value) => f(f(clamp01f(value) * 4.0) - 2.0),
  /// Dirty Roller: the fraction of kRollerDepth a mark takes, 0 to 1.
  rollerStrength: (value) => clamp01f(value),
  /// Speed: film seconds per host second, 1 to 256, geometric. 0 is 1x; 0.75
  /// is exactly 64x.
  speedFactor: (value) => Math.pow(2.0, 8.0 * clamp01f(value)),
  speedParam: (factor) => clamp01f(Math.log2(Math.max(factor, 1.0)) / 8.0),
  /// Border: 0 the image full-frame, 1 the print in its frame.
  border: (value) => clamp01f(value),
  /// exp( -Ea / R ( 1 / T - 1 / Tref ) ), relative to 24 degC.
  arrheniusFactor(celsius, activation) {
    const t = celsius + 273.15;
    const tRef = MODEL_H.kReferenceC + 273.15;
    return Math.exp(-activation / MODEL_H.kGasConstant * (1.0 / t - 1.0 / tRef));
  },
  /// An option's value to its index, rounded and clamped.
  optionIndex: (value, count) => Math.min(Math.max(lround(f(value)), 0), count - 1),
  /// The print's layout in the output raster, in pixels, y UP. At Border 1 the
  /// print is 88 x 107 mm, 92% of the shorter fit, centred, and the image
  /// window 79 x 79 mm inside it: 4.5 mm at the sides, 6 mm at the top, 22 mm
  /// at the bottom. Between, every edge moves linearly.
  printLayout(width, height, border) {
    const b = controls.border(border);
    const W = width;
    const H = height;
    const printW = 88.0;
    const printH = 107.0;
    const scale = 0.92 * Math.min(W / printW, H / printH);
    const pw = printW * scale;
    const ph = printH * scale;
    const px0 = 0.5 * (W - pw);
    const py0 = 0.5 * (H - ph);
    const ix0 = px0 + 4.5 * scale;
    const iy0 = py0 + 22.0 * scale;
    const ix1 = ix0 + 79.0 * scale;
    const iy1 = iy0 + 79.0 * scale;
    const lerp = (a, c, t) => f(a + (c - a) * t);
    return {
      printMin: [lerp(0.0, px0, b), lerp(0.0, py0, b)],
      printMax: [lerp(W, px0 + pw, b), lerp(H, py0 + ph, b)],
      imageMin: [lerp(0.0, ix0, b), lerp(0.0, iy0, b)],
      imageMax: [lerp(W, ix1, b), lerp(H, iy1, b)],
    };
  },
};

/// Instant.cpp's file-scope constants.
const K_SLACK = 1e-9;

/// The harness-only hooks, at what the shipped plugin sets them to.
const PERTURB = 0;
const PROBE = 0;

//===========================================================================
// The renderer: Instant::InitGL and Instant::ProcessOpenGL, in their order.
//===========================================================================

/// What the line under the canvas reports. Filled by the renderer.
const telemetry = {
  ticked: false,
  now: 0, dt: 0, age: 0, dAge: 0, mode: 1, hasPrint: false, takes: 0,
  sinceTake: 0, interval: 16, speed: 64, celsius: 24, kDye: 1, kStop: 1,
  meterMean: null, width: 0, height: 0, resamples: 0,
  exactVerdict: '', missing: [],
};

/// The Take event, module-level so the button under the canvas can reach it.
/// `takeWas` and `takePending` are Instant's members of the same names.
const take = {
  takePending: false,
  takeWas: 0.0,
  /// Instant::SetFloatParameter for PT_TAKE: an event arrives as 1.0 on press
  /// and 0.0 on release; the take is the press. Edge-triggered, so a host
  /// restating 1.0 does not fire it again.
  set(value) {
    value = f(value);
    if (value >= 0.5 && this.takeWas < 0.5) this.takePending = true;
    this.takeWas = value;
  },
};

/// For a driven check (AGENTS.md, "The browser demo"). `fresh()` puts the
/// renderer back to a newly instantiated plugin; `afterRender(gl, input)` runs
/// inside the frame, before the canvas is composited, so it can read it back.
const hooks = { afterRender: null, fresh: null };

function createRenderer(gl, quad) {
  // The kit asks for EXT_color_buffer_float (needFloat); this one is asked for
  // here. The print pass samples the RG32F dose with linear filtering, and the
  // meter's R32F mip chain cannot be generated without it: a page that ran
  // anyway would print from zero dose and read a meter of nothing.
  if (!gl.getExtension('OES_texture_float_linear')) {
    throw new GLError('OES_texture_float_linear is missing. The plugin filters its RG32F development state and averages its R32F meter through a mip chain, and without it both would silently read zero.');
  }

  // The plugin's exact constants text, tried and reported, never used: see
  // the header. If a browser ever accepts it the page still compiles the
  // respelt text, so the picture does not depend on which browser is looking.
  const vertex = assemble(K_CONSTANTS, VERTEX_BODY, false);
  try {
    new Program(gl, vertex, assemble(K_CONSTANTS, PRINT_BODY, true), 'print (exact)').dispose();
    telemetry.exactVerdict = 'this browser accepted the plugin’s exact constants text (the page compiles the respelt one regardless)';
  } catch (error) {
    const first = String(error.message).split('\n').find((line) => line.startsWith('ERROR')) ?? error.message;
    telemetry.exactVerdict = `the plugin’s exact constants text is refused by this browser’s GLSL ES compiler — ${first.trim()}`;
  }

  const constants = esConstants(K_CONSTANTS);
  const program = (body, withModel, label) => new Program(gl, vertex, assemble(constants, body, withModel), label);
  const captureShader = program(CAPTURE_BODY, true, 'capture');
  const meterShader = program(METER_BODY, true, 'meter');
  const developShader = program(DEVELOP_BODY, true, 'develop');
  const resampleShader = program(RESAMPLE_BODY, false, 'resample');
  const printShader = program(PRINT_BODY, true, 'print');

  // PassBuffer::Ensure( w, h, format, Sampling::Linear ) for the capture, the
  // doses and a resize's scratch; Sampling::Mipmapped for the meter.
  const linear = { filter: 'linear' };
  let capture = new PassBuffer(gl, linear);
  const dose = [new PassBuffer(gl, linear), new PassBuffer(gl, linear)];
  let scratch = new PassBuffer(gl, linear);
  const meter = new PassBuffer(gl, { filter: 'linear', mip: true });
  const isValid = (buffer) => buffer.texture !== null;

  // PassBuffer::Ensure allocates AND clears; the kit's ensure() only
  // allocates (WebGL zero-fills new storage, but the plugin does not rely on
  // that and neither does this). Returns false never: the kit throws.
  const ensure = (buffer, width, height, format) => {
    const was = isValid(buffer) && buffer.width === width && buffer.height === height && buffer.internalFormat === format;
    if (was) return;
    buffer.ensure(width, height, format);
    buffer.clearTo(0.0, 0.0, 0.0, 0.0);
  };

  // The meter's top level, read back once per take for the line under the
  // picture only. Never fed to anything: the print pass reads the same texel
  // on the GPU.
  const meterFbo = gl.createFramebuffer();
  const meterPixel = new Float32Array(4);
  const readMeter = () => {
    try {
      gl.bindFramebuffer(gl.FRAMEBUFFER, meterFbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, meter.texture, MODEL_H.kMeterLevel);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) return null;
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.FLOAT, meterPixel);
      return meterPixel[0];
    } catch {
      return null;
    } finally {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    }
  };

  //--- Instant's members ----------------------------------------------------
  let current = 0;
  let hasPrint = false;
  let lastTake = 0.0;
  let age = 0.0;
  let takes = 0;
  let lastWidth = 0;
  let lastHeight = 0;
  let lastNow = -1.0;

  /// Instant::InitGL's state reset (the GL objects are made above, once).
  /// takePending is deliberately left alone: a press that arrived before the
  /// GL context existed is still a press.
  const initGL = () => {
    hasPrint = false;
    age = 0.0;
    current = 0;
    lastWidth = lastHeight = 0;
  };
  initGL();

  hooks.fresh = () => {
    capture.dispose();
    dose[0].dispose();
    dose[1].dispose();
    scratch.dispose();
    meter.dispose();
    take.takePending = false;
    take.takeWas = 0.0;
    lastTake = 0.0;
    takes = 0;
    lastNow = -1.0;
    initGL();
    telemetry.takes = 0;
    telemetry.resamples = 0;
    telemetry.meterMean = null;
  };

  /// Instant::rescale: carry the capture and the development state across to
  /// a new raster, resampled, instead of letting a reallocation clear them.
  /// The C++ swaps the GL ids behind two PassBuffers; here the objects change
  /// places.
  const rescale = (width, height) => {
    const items = [
      { get: () => capture, put: (b) => { capture = b; }, format: gl.RGBA16F },
      { get: () => dose[current], put: (b) => { dose[current] = b; }, format: gl.RG32F },
    ];
    for (const item of items) {
      const b = item.get();
      if (!isValid(b) || (b.width === width && b.height === height)) continue;
      ensure(scratch, width, height, item.format);
      scratch.bind();
      resampleShader.use();
      bindTexture(gl, 0, b.texture);
      resampleShader.setSampler('Source', 0);
      resampleShader.set('HalfTexel', f(0.5 / b.width), f(0.5 / b.height));
      gl.disable(gl.BLEND);
      quad.draw();
      bindTexture(gl, 0, null);
      // The old buffer is now `scratch`, at the old size, and is released.
      item.put(scratch);
      scratch = b;
      scratch.dispose();
    }
    telemetry.resamples += 1;
  };

  return {
    render({ input, params, width: vpW, height: vpH, time }) {
      const p = (id) => params.get(id);
      const width = input.width;
      const height = input.height;
      // The host's viewport: the whole canvas, origin 0.
      const hostViewport = [0, 0, vpW, vpH];

      //------------------------------------------------------------------
      // The clock, reduced in double; the shaders see only the age at the
      // frame's start and the frame's length, both relative to the take.
      //------------------------------------------------------------------
      const now = time;
      let dt = MODEL_H.kNominalFrame;
      if (lastNow >= 0.0) dt = Math.min(Math.max(now - lastNow, 0.0), MODEL_H.kMaxFrameDelta);
      lastNow = now;

      //------------------------------------------------------------------
      // What the controls say.
      //------------------------------------------------------------------
      const film = controls.optionIndex(p('film'), CONTROLS_H.kFilmCount);
      const celsius = controls.temperatureC(p('temperature'));
      const expired = controls.expired(p('expired'));
      const mode = controls.optionIndex(p('mode'), CONTROLS_H.kModeCount);
      const interval = controls.intervalSeconds(p('interval'));
      const stops = controls.exposureStops(p('exposure'));
      const spread = controls.optionIndex(p('spread'), CONTROLS_H.kSpreadCount);
      const roller = controls.rollerStrength(p('roller'));
      const speed = controls.speedFactor(p('speed'));
      const border = controls.border(p('border'));
      const mixAmount = Math.min(Math.max(f(p('mix')), 0.0), 1.0);

      //------------------------------------------------------------------
      // The take. Continuous: the first frame, a press, or Interval host
      // seconds since the last take. Take: a press, and nothing else. A
      // clock that runs backwards restarts the interval and keeps the print.
      //------------------------------------------------------------------
      if (now < lastTake - K_SLACK) lastTake = now;
      let doTake = take.takePending;
      if (mode === CONTROLS_H.kModeContinuous && (!hasPrint || now - lastTake >= interval - K_SLACK)) doTake = true;
      take.takePending = false;

      let ageStart = age;
      let dAge = 0.0;
      if (doTake) {
        hasPrint = true;
        lastTake = now;
        age = ageStart = 0.0;
        takes += 1;
      } else if (hasPrint) {
        dAge = dt * speed;
        age += dAge;
      }

      const noArrhenius = (PERTURB & 4) !== 0;
      const kDye = noArrhenius ? 1.0 : controls.arrheniusFactor(celsius, MODEL_H.kDyeActivation);
      const kStop = noArrhenius ? 1.0 : controls.arrheniusFactor(celsius, MODEL_H.kStopActivation);

      //------------------------------------------------------------------
      // Buffers: every allocation before anything binds a texture. A resize
      // carries the print across, resampled.
      //------------------------------------------------------------------
      const resized = lastWidth !== 0 && (lastWidth !== width || lastHeight !== height);
      if (resized && hasPrint && !doTake && (PERTURB & 32) === 0) rescale(width, height);
      lastWidth = width;
      lastHeight = height;

      ensure(capture, width, height, gl.RGBA16F);
      ensure(dose[0], width, height, gl.RG32F);
      ensure(dose[1], width, height, gl.RG32F);
      ensure(meter, MODEL_H.kMeterSize, MODEL_H.kMeterSize, gl.R32F);

      // A fresh print has no reagent anywhere.
      if (doTake) {
        dose[current].clearTo(0.0, 0.0, 0.0, 0.0);
        dose[1 - current].clearTo(0.0, 0.0, 0.0, 0.0);
      }

      //------------------------------------------------------------------
      // The layout and the crop: the image window shows the centre of the
      // clip's frame, cropped to the window's aspect.
      //------------------------------------------------------------------
      const layout = controls.printLayout(hostViewport[2], hostViewport[3], border);
      const iw = Math.max(1.0, f(layout.imageMax[0] - layout.imageMin[0]));
      const ih = Math.max(1.0, f(layout.imageMax[1] - layout.imageMin[1]));
      const windowAspect = iw / ih;
      const sourceAspect = width / height;
      const cropX = f(windowAspect < sourceAspect ? windowAspect / sourceAspect : 1.0);
      const cropY = f(windowAspect < sourceAspect ? 1.0 : sourceAspect / windowAspect);

      gl.disable(gl.BLEND);

      //------------------------------------------------------------------
      // 1. Capture: the light at the take.
      //------------------------------------------------------------------
      if (doTake || (hasPrint && (PERTURB & 64) !== 0)) {
        capture.bind();
        captureShader.use();
        bindTexture(gl, 0, input.texture);
        captureShader.setSampler('InputTexture', 0);
        quad.draw();
        bindTexture(gl, 0, null);
      }
      if (doTake) {
        // The meter reads what the film will see, at the take, once.
        meter.bind();
        meterShader.use();
        bindTexture(gl, 0, capture.texture);
        meterShader.setSampler('Capture', 0);
        meterShader.set('CropScale', cropX, cropY);
        quad.draw();
        bindTexture(gl, 0, null);
        meter.generateMipmap();
        telemetry.meterMean = readMeter();
      }

      //------------------------------------------------------------------
      // 2. Develop: this frame's film-seconds into the doses.
      //------------------------------------------------------------------
      if (hasPrint && dAge > 0.0) {
        const from = dose[current];
        const to = dose[1 - current];
        to.bind();
        developShader.use();
        bindTexture(gl, 0, from.texture);
        developShader.setSampler('Previous', 0);
        developShader.set('Size', f(width), f(height));
        developShader.set('AgeStart', f(ageStart));
        developShader.set('DAge', f(dAge));
        developShader.set('KDye', f(kDye));
        developShader.set('KStop', f(kStop));
        developShader.set('StopDose', f(MODEL_H.kStopDose));
        developShader.set('CropScale', cropX, cropY);
        developShader.setInt('Spread', spread);
        developShader.set('Expired', expired);
        developShader.setInt('Perturb', PERTURB);
        quad.draw();
        bindTexture(gl, 0, null);
        current = 1 - current;
      }

      //------------------------------------------------------------------
      // 3. The print, straight to the host.
      //------------------------------------------------------------------
      {
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        gl.viewport(hostViewport[0], hostViewport[1], hostViewport[2], hostViewport[3]);

        // The stock, with Expired applied, and the balance that makes a
        // neutral grey come out neutral at the stop at 24 degC.
        const stock = MODEL_H.kStocks[film];
        const tauStock = film === 2 ? MODEL_H.kTauVintage : MODEL_H.kTauColour;
        const tau = [0, 0, 0];
        const balance = [0, 0, 0];
        const dmin = [0, 0, 0];
        const dmax = [0, 0, 0];
        for (let i = 0; i < 3; i += 1) {
          tau[i] = stock.mono ? MODEL_H.kTauMono : tauStock[i];
          balance[i] = 1.0 / (1.0 - Math.exp(-MODEL_H.kStopDose / tau[i]));
          const loss = stock.mono ? MODEL_H.kExpiredDmaxLoss : (i === 0 ? MODEL_H.kExpiredCyanLoss : MODEL_H.kExpiredDmaxLoss);
          dmax[i] = stock.dmax[i] * (1.0 - loss * expired);
          dmin[i] = stock.dmin[i] + (stock.mono ? 0.5 : (i === 0 ? 0.0 : 1.0)) * MODEL_H.kExpiredStain * expired;
        }
        const white = stock.mono ? MODEL_H.kWhiteMono : MODEL_H.kWhiteColour;
        const ox = f(hostViewport[0]);
        const oy = f(hostViewport[1]);

        printShader.use();
        bindTexture(gl, 0, dose[current].texture);
        bindTexture(gl, 1, capture.texture);
        bindTexture(gl, 2, input.texture);
        bindTexture(gl, 3, meter.texture);

        printShader.setSampler('Dose', 0);
        printShader.setSampler('Capture', 1);
        printShader.setSampler('Source', 2);
        printShader.setSampler('Meter', 3);
        printShader.set('MeterLevel', f(MODEL_H.kMeterLevel));
        // The input is exactly its texture's raster: GetMaxGLTexCoords is 1, 1.
        printShader.set('MaxUV', 1.0, 1.0);
        printShader.set('PrintMin', f(ox + layout.printMin[0]), f(oy + layout.printMin[1]));
        printShader.set('PrintMax', f(ox + layout.printMax[0]), f(oy + layout.printMax[1]));
        printShader.set('ImageMin', f(ox + layout.imageMin[0]), f(oy + layout.imageMin[1]));
        printShader.set('ImageMax', f(ox + layout.imageMax[0]), f(oy + layout.imageMax[1]));
        printShader.set('CropScale', cropX, cropY);
        printShader.setInt('HasPrint', hasPrint ? 1 : 0);
        printShader.setInt('Mono', stock.mono ? 1 : 0);
        printShader.set('ExposureGain', f(Math.pow(2.0, stops)));
        printShader.set('Toe', f(stock.shoulder - stock.latitude));
        printShader.set('Latitude', f(stock.latitude));
        printShader.set('Dmin', f(dmin[0]), f(dmin[1]), f(dmin[2]));
        printShader.set('Dmax', f(dmax[0]), f(dmax[1]), f(dmax[2]));
        printShader.set('Balance', f(balance[0]), f(balance[1]), f(balance[2]));
        printShader.set('Tau', f(tau[0]), f(tau[1]), f(tau[2]));
        printShader.set('TauOp', f(MODEL_H.kTauOpacifier));
        printShader.set('White', white[0], white[1], white[2]);
        printShader.set('Roller', roller);
        printShader.setInt('Spread', spread);
        printShader.set('Expired', expired);
        printShader.set('MixAmount', mixAmount);
        printShader.setInt('Probe', PROBE);
        printShader.setInt('Perturb', PERTURB);
        gl.disable(gl.BLEND);
        quad.draw();
      }

      telemetry.ticked = true;
      telemetry.now = now;
      telemetry.dt = dt;
      telemetry.age = age;
      telemetry.dAge = dAge;
      telemetry.mode = mode;
      telemetry.hasPrint = hasPrint;
      telemetry.takes = takes;
      telemetry.sinceTake = now - lastTake;
      telemetry.interval = interval;
      telemetry.speed = speed;
      telemetry.celsius = celsius;
      telemetry.kDye = kDye;
      telemetry.kStop = kStop;
      telemetry.width = width;
      telemetry.height = height;
      // A uniform name the shader does not have is a dead control (glUniform
      // on -1 is a no-op); the kit counts them. Empty is right.
      telemetry.missing = [...captureShader.missing, ...meterShader.missing, ...developShader.missing, ...resampleShader.missing, ...printShader.missing];

      if (hooks.afterRender) hooks.afterRender(gl, input);
      bindTexture(gl, 3, null);
      bindTexture(gl, 2, null);
      bindTexture(gl, 1, null);
      bindTexture(gl, 0, null);
    },
  };
}

//===========================================================================
// The controls, read out of Instant::Instant(). Same names, same groups, same
// order, same defaults, same dropdown elements. Absent: Take, which is the
// button under the canvas (FF_TYPE_EVENT), and the About block.
//===========================================================================

const std = (id, name, def, group, extra = {}) => ({ id, name, type: 'standard', default: f(def), group, ...extra });
const opt = (id, name, elements, def, group, hint) => ({ id, name, type: 'option', elements, default: def, group, hint });
const signed = (x, digits) => `${x >= 0 ? '+' : '−'}${Math.abs(x).toFixed(digits)}`;
const secondsText = (s) => (s >= 10 ? `${s.toFixed(0)} s` : `${s.toFixed(2)} s`);
const speedText = (x) => (x >= 10 ? `${x.toFixed(0)}×` : `${x.toFixed(2)}×`);

const demo = mountDemo({
  name: 'Instant',
  pluginId: 'IN01',
  kind: 'effect',
  tagline:
    'Instant film, developing in front of you. A Take exposes the clip’s frame onto an integral instant print, metered by the camera. The rollers spread the reagent up from the pod at the bottom border, and the picture comes up from a dark green-grey, pale and cyan first, warming as the magenta and yellow dyes arrive: each dye layer first-order, at a rate set by an Arrhenius temperature law, until the timing layer’s pH drop stops it. Cold film stops short, pale and cyan; hot film finishes, warm; a short spread leaves the corners dark, a dirty roller repeats its mark. Speed scales time for live use; 1× is a real print.',
  repo: 'https://github.com/stoatworks-labs/instant',

  // The stock sentence says "same maths", which is only most of the truth
  // here: the shaders are the plugin's, the state they run on is a port.
  blurb:
    'It is Instant’s own GLSL — the capture, meter, develop, resample and print passes and the model library they share — ported from the repository to WebGL2, so the capture, the camera’s meter, the dye and stop doses and the print run on your GPU over the plugin’s own float buffers. The CPU half (the clock and the film age, the take, the Arrhenius factors, the print’s layout, the stock arithmetic and every control’s law) is ported to JavaScript by hand; nothing checks that port but a reader. It runs on generated clips in this page, with the plugin’s own parameters and no install.',

  // The capture is RGBA16F, the doses RG32F and the meter R32F render
  // targets, as in the plugin.
  needFloat: true,
  // Outside the print (Border above 0) the output is colour 0 and alpha 0,
  // so the print floats over whatever is behind it.
  showBackdrop: true,

  params: [
    opt('film', 'Film', FILM_NAMES, 0, 'Film',
      'The stock. Colour: three dye layers, cyan 70 s, magenta 120 s, yellow 200 s at 24 °C, a four-stop latitude. Black & White: one image layer by luminance, τ 90 s, a longer latitude and deeper black. Vintage: slower warm layers (90, 170, 230 s), a shorter latitude, a warm base stain and weaker cyan. Every τ is the plugin’s stated assumption, not a measurement.'),
    std('temperature', 'Temperature', controls.temperatureParam(MODEL_H.kReferenceC), 'Film', {
      display: (v) => `${controls.temperatureC(v).toFixed(1)} °C`,
      hint: '4 to 36 °C, linear; 0.625 is exactly 24 °C, the reference every τ is stated at. Each rate is an Arrhenius factor: the dye’s at 67.41 kJ/mol (fitted to a published black-and-white development chart, an assumption for dye), the timing layer’s at half that (assumed). Cold, the dye slows more than the stop does, so development ends short: a light, low-contrast print with a cyan cast in this model (the manufacturer’s page describes the cold cast as green; the cyan is the model’s). Hot, all three layers finish: warmer. It changes the print that is developing, too.',
    }),
    std('expired', 'Expired', 0.0, 'Film', {
      display: (v) => `${(100 * controls.expired(v)).toFixed(0)}%`,
      hint: 'Film past its date: at 1, Dmax falls 30% (cyan 45%, so old prints go pink), the base stains 0.14 density in magenta and yellow, and the dried paste reaches 30% less far, least at the corners.',
    }),

    opt('mode', 'Mode', MODE_NAMES, CONTROLS_H.kModeContinuous, 'Exposure',
      'Take: the clip passes through untouched (a viewfinder) until the Take button under the picture is pressed; then one print, which keeps developing from that moment whatever the clip does. Continuous (the default): the first frame takes a print, and so does every Interval seconds, or a press. Switching keeps the print that exists.'),
    std('interval', 'Interval', controls.intervalParam(16.0), 'Exposure', {
      display: (v) => secondsText(controls.intervalSeconds(v)),
      hint: 'Host seconds between prints in Continuous mode, 0.5 to 128, geometric; 0.625 is exactly 16 s. At the default Speed a print is finished 9.4 s after its take, so at 16 s it holds for about six seconds before the next.',
    }),
    std('exposure', 'Exposure', 0.5, 'Exposure', {
      display: (v) => `${signed(controls.exposureStops(v), 2)} stops`,
      hint: 'The lighten/darken wheel, −2 to +2 stops on top of what the camera’s averaging meter chose at the take (which brings the image window’s mean to mid grey, within −2..+3 stops). 0.5 is exactly 0. It changes the developing print too.',
    }),

    opt('spread', 'Spread', SPREAD_NAMES, CONTROLS_H.kSpreadFull, 'Chemistry',
      'How the rollers spread the reagent from the pod at the bottom. Full: past the top everywhere. Short: too little paste, the corners never reached, so they stay the dark negative. Uneven: lanes of paste at different speeds and depths, a ragged reach.'),
    std('roller', 'Dirty Roller', 0.0, 'Chemistry', {
      display: (v) => `${(100 * MODEL_H.kRollerDepth * controls.rollerStrength(v)).toFixed(0)}% at a mark`,
      hint: 'Dirt on a roller: a band across the print once per revolution, at 5/16 of the image height, lighter where the dirt kept dye from reaching; uneven along the roller, the same at every repeat.',
    }),
    std('speed', 'Speed', controls.speedParam(64.0), 'Chemistry', {
      display: (v) => speedText(controls.speedFactor(v)),
      hint: 'Film seconds per host second, 1× to 256×, geometric; 0.75 is exactly 64×, the default, where the picture comes up in about a second and development ends 9.4 s after the take. 1× (slider at 0) is a real print: ten minutes to the stop.',
    }),

    std('border', 'Border', 1.0, 'Print', {
      display: (v) => (controls.border(v) === 0 ? 'full frame' : controls.border(v) === 1 ? 'print' : `${(100 * controls.border(v)).toFixed(0)}%`),
      hint: 'At 1 the print in its frame: 88 × 107 mm with a 79 mm square window, thicker at the bottom where the pod was; the window shows the centre of the clip. At 0 the image fills the frame with no border. Between, every edge moves linearly. Outside the print the output is transparent.',
    }),
    std('mix', 'Mix', 1.0, 'Print', {
      hint: 'The print against the input; alpha fades from the print’s to the source’s.',
    }),
  ],

  // A moving scene first: the print holds the moment of the take while the
  // clip moves on. The bars and the ramp make the colour drift and the curve
  // readable; the spot clip shows the meter lifting a dark clip.
  sources: ['scene', 'bars', 'ramp', 'grid', 'spot', 'alpha', 'detail'],

  // The plugin ships no factory presets. These are the page's own, expressed
  // entirely in the plugin's parameters and reachable with the controls.
  presets: {
    'Real time (1×: ten minutes to the stop)': { speed: controls.speedParam(1.0), interval: 1.0 },
    'Slow (8×)': { speed: controls.speedParam(8.0), interval: 1.0 },
    'Take mode (then press Take)': { mode: CONTROLS_H.kModeTake },
    'Cold room, 14 °C': { temperature: controls.temperatureParam(14.0) },
    'Hot day, 34 °C': { temperature: controls.temperatureParam(34.0) },
    'Black & White': { film: 1 },
    'Vintage': { film: 2 },
    'Expired, short spread': { expired: 0.8, spread: CONTROLS_H.kSpreadShort },
    'Uneven spread, dirty roller': { spread: CONTROLS_H.kSpreadUneven, roller: 1.0 },
    'Full frame (Border 0)': { border: 0.0 },
    'Lighter, +1 stop': { exposure: 0.75 },
  },

  differences: [
    'The CPU half of this plugin is a PORT, not the plugin’s own code. Instant keeps its clock and state in C++: the film age since the take in double, the take’s edge trigger and the Continuous interval, the two Arrhenius factors, the buffers and when they are cleared, the resample-and-swap that carries the print across a resize, the print’s layout in millimetres and the crop, the stock arithmetic (Expired’s losses and stain, the balance that makes a neutral grey neutral at the stop) and every slider’s law in Controls.cpp. All of that is ported here line for line, in JavaScript doubles as the plugin keeps them, rounded to float where the plugin hands a float uniform over; Model.h’s numbers are copied by a script. Nothing checks a port but a reader; the repository’s intest --develop, --order, --arrhenius, --front, --roller, --take and --meter check the C++ against the model’s statement and have never heard of this page.',
    'The shaders are not a port. The vertex shader, the model library and the capture, meter, develop, resample and print bodies are the plugin’s own GLSL, assembled as Shaders.cpp assembles them, after the constants block the plugin writes from Model.h at run time — the same text, which demo/tools/check_shaders.py compares, whole and assembled, with what the plugin compiles, and which fails the repository’s verify script if a character drifts.',
    `One spelling in that constants block is the page’s. The plugin prints Model.h’s numbers with C’s %.9g, so ${RESPELT.length} integer-valued floats come out as integers (${RESPELT.join(', ')}: “const float kKnee = 6;”). Desktop GLSL 4.10 converts the int to float; GLSL ES 3.00 has no implicit conversions and refuses the line. The page tries the plugin’s exact text on every load and reports the compiler’s verdict under the picture, then compiles those lines — and only lines of exactly that shape — spelt with a “.0”. Each is the same float; no other character of any shader changes.`,
    'The buffers are the plugin’s: the capture RGBA16F, two RG32F dose buffers ping-ponged, all linear-sampled, and the camera’s meter a 64 × 64 R32F averaged through its mip chain to one texel. WebGL2 needs EXT_color_buffer_float to render into them and OES_texture_float_linear to filter them and build the mip chain; the page refuses to start without either rather than fall back to 8 bits. The meter’s average is the browser’s glGenerateMipmap, as the plugin’s is the driver’s; on a textured frame two drivers need not average identically.',
    'The clock is the kit’s, in declared seconds; the plugin’s unit vote and its wall-clock fallback never run. Everything downstream is the plugin’s rule: dt is the frame delta clamped to 0–0.25 s (the kit itself caps a delta at 0.1 s), a nominal 1/60 on the first frame, and the print ages dt × Speed film-seconds. A paused page renders only when a control moves, and each such frame is worth 0 s, so a paused print holds, as in a paused host (a real print would go on developing). Restart sends the clock back to 0, which the plugin reads as a host clock running backwards: the interval restarts and the print is kept. Step adds exactly 1/60 s.',
    'Take is FF_TYPE_EVENT in the plugin. The kit has no control for an event, so it is the button under the picture rather than a row in the inspector. A press is delivered as the plugin’s SetFloatParameter delivers it — 1.0 then 0.0, edge-triggered on the crossing of 0.5 — and consumed on the next rendered frame: in Take mode it takes the print, in Continuous mode it takes at once and restarts the interval. In embed mode there is no button, so Take mode stays a viewfinder there.',
    'The chemistry’s numbers are the plugin’s stated model, not measurements of a film: the dye activation energy is fitted to a published black-and-white development chart (an assumption for dye), and the timing layer’s activation energy, every time constant, the stop at 600 s, the front speed, the roller, the curve, the densities and the colours are assumptions, chosen and judged by eye. The cold print’s cast is cyan in this model; the manufacturer describes it as green.',
    'The reagent front is modelled (each row starts developing at its distance from the pod over the front’s speed, one image height a film-second) but it is not something you can watch: it crosses the print in about one film-second, under an opacifier that takes about a minute to clear, at every Speed.',
    'The plugin stores each host value as a float; the page’s sliders are doubles, so every value is rounded through Math.fround before its law is applied, and the defaults are the plugin’s float defaults (Temperature 0.625 = 24 °C, Interval 0.625 = 16 s, Speed 0.75 = 64×, Mode Continuous).',
    'The harness-only Perturb and Probe uniforms are set to what the shipped plugin sets them to, 0. The eight negative controls and the raw density probe intest reads through them are not on this page. The About block is absent, as on every page in this suite.',
    'The plugin’s proof — each dye layer’s τ fitted out of the picture, cyan leading at 240 s and the balance moving only toward neutral, the Arrhenius ratio between 14 and 34 °C, each row starting at distance over speed, the roller’s period whole-pixel and fractional, a print that develops from its capture whatever the clip does and survives a resize, the meter — is an offline harness in the repository, at two rasters and on a software renderer. Nothing on this page measures anything; the line under the picture reports what the ported clock and take are doing.',
  ],

  createRenderer,
});

// For a driven check (AGENTS.md, "The browser demo"): the kit's state and
// redraw, the telemetry, the Take event and the hooks, so a script can pause,
// set the clock to n / 60 and render one frame at a time, as
// `intest --pipe --fps 60` clocks its frames.
window.__instantDemo = { demo, telemetry, hooks, take, controls };

//---------------------------------------------------------------------------
// Under the canvas: the Take button — the plugin's own FF_TYPE_EVENT control,
// which the kit's inspector cannot draw — and a line reporting what the
// ported clock and take are doing. Skipped in embed mode, where there is no
// reader and no button.
//---------------------------------------------------------------------------
if (demo && !new URLSearchParams(window.location.search).has('embed')) {
  const stage = document.querySelector('.stage');
  if (stage) {
    const row = document.createElement('div');
    row.className = 'transport';
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'btn';
    button.textContent = 'Take';
    button.id = 'take';
    button.title = 'The plugin’s Take event: expose the clip’s frame now and start a new print. In Continuous mode it also restarts the interval.';
    button.addEventListener('click', () => {
      // Instant::SetFloatParameter for PT_TAKE, as a host delivers an event:
      // 1.0 on press, 0.0 on release. The next ProcessOpenGL consumes it. A
      // paused page needs that frame asked for.
      take.set(1.0);
      take.set(0.0);
      if (!demo.state.playing) demo.redraw();
    });
    const label = document.createElement('span');
    label.className = 'transport__field';
    label.textContent = 'Take — the plugin’s event control, a button here because the inspector has no event row.';
    row.append(button, label);

    const line = document.createElement('p');
    line.className = 'stage__status';
    line.id = 'telemetry';
    const verdict = document.createElement('p');
    verdict.className = 'stage__status';
    verdict.id = 'verdict';
    stage.append(row, line, verdict);
    setInterval(() => {
      if (!telemetry.ticked) return;
      const t = telemetry;
      const stopAt = MODEL_H.kStopDose / t.kStop;//film-seconds after the front arrived
      const modeText = t.mode === CONTROLS_H.kModeContinuous
        ? `Continuous: next print in ${Math.max(0, t.interval - t.sinceTake).toFixed(1)} s`
        : (t.hasPrint ? 'Take: holding the last print' : 'Take: viewfinder, waiting for Take');
      const meterText = t.meterMean === null ? 'n/a'
        : (() => {
          const gain = Math.min(Math.max(MODEL_H.kMeterTarget / Math.max(t.meterMean, 1e-9), Math.pow(2, MODEL_H.kMeterMinStop)), Math.pow(2, MODEL_H.kMeterMaxStop));
          return `mean ${t.meterMean.toFixed(4)}, ${signed(Math.log2(gain), 2)} stops`;
        })();
      line.textContent =
        `${modeText}. ${t.hasPrint ? `Print ${t.age.toFixed(1)} film-s old (${Math.min(100, (100 * t.age) / stopAt).toFixed(0)}% of the way to the stop at ${stopAt.toFixed(0)} film-s, at the pod edge)` : 'No print'}; `
        + `${speedText(t.speed)}, ${t.celsius.toFixed(1)} °C (dye rate ×${t.kDye.toFixed(3)}, stop ×${t.kStop.toFixed(3)}); meter at the take: ${meterText}; `
        + `clock ${t.now.toFixed(3)} s, this frame ${t.dt.toFixed(4)} s = ${t.dAge.toFixed(3)} film-s; ${t.width} × ${t.height}; ${t.takes} take${t.takes === 1 ? '' : 's'}`
        + `${t.missing.length ? `; UNMATCHED UNIFORMS: ${t.missing.join(', ')}` : ''}.`;
      verdict.textContent = `Constants block: ${t.exactVerdict}.`;
    }, 250);
  }
}
