#pragma once

/**
	The print as numbers. The arithmetic is the `kModel` GLSL library and the
	pass bodies in Shaders.cpp; the harness takes these constants and measures
	everything else out of the picture.

	**Exposure.** The clip's frame at the Take, sRGB-decoded to linear and
	premultiplied onto black by its alpha (the film sees no light where the
	clip is transparent), times the gain the camera's meter chose (below),
	times 2^Exposure stops. Per channel, x = log10 H.
	Colour stocks expose three layers (red-, green-, blue-sensitive, which
	release cyan, magenta and yellow dye developers); the black-and-white
	stock exposes one, by BT.709 luminance.

	**The characteristic curve.** Coverage, as in rebate and wetplate:

	    c( x ) = ( sp( x - Toe ) - sp( x - Toe - Latitude ) ) / Latitude,
	    sp( u ) = ln( 1 + e^( Knee u ) ) / Knee,  Toe = Shoulder - Latitude.

	An integral print is a POSITIVE process: where silver develops, the dye
	developer above it is immobilised and never reaches the image layer. So
	the dye a layer will deliver is

	    Dinf = Balance * depth * ( Dmin + ( Dmax - Dmin ) ( 1 - c( x ) ) ).

	**Development.** Each dye layer approaches Dinf first-order:

	    D( t ) = Dinf ( 1 - e^( -A_i / tau_i ) ),  A_i = the layer's dye dose, in seconds at 24 degC,

	where A_i is the time since the reagent front reached the point, each
	film-second weighted by the layer's Arrhenius rate at the temperature it
	passed at (A_i = t - t0 at 24 degC). The same reagent carries the OPACIFIER, a dark
	layer that shields the negative from light while it develops, which clears
	as the timing layer drops the pH:

	    O( t ) = O0 e^( -S / tau_op ),  S = the stop dose, weighted by the
	                                     timing layer's own (lower) Arrhenius rate.

	The same pH drop ENDS development: once S reaches `kStopDose` no dye moves
	any more. So temperature changes the picture and not only the speed.
	Each dye layer has its OWN activation energy (`kLayerActivation`), so each
	layer's dose A_i is its own sum of film-seconds, weighted by its own rate.
	Cold, the dyes slow more than the timing layer does, and development ends
	short: a lighter print, lower in contrast. Magenta slows the most, so it is
	cut shortest and the grey comes out GREEN, as the manufacturer's support
	page (see ATTRIBUTIONS.md) says of a cold print. Hot, every layer all but
	finishes and the slow yellow over-reaches its balance: a warm yellow/red
	cast, as the page says of a hot one. Two of the three activation energies
	are FITTED to that page's description (tools/cast_fit.py), not measured;
	they are stated as such below.

	**Presentation.** The print is seen by reflection off the reagent's white
	pigment, through the dye image and the opacifier:
	R = White 10^( -( D + O ) ), per channel. Where the reagent never reached,
	there is no white pigment and no image: the dark negative shows.
*/
namespace instant::model
{

/// The gas constant, J / ( mol K ), CODATA 2018.
constexpr double kGasConstant = 8.314462618;

/// The reference temperature, degC: every tau below is at this temperature.
/// A warm room; inside the 13-28 degC the manufacturer recommends.
constexpr double kReferenceC = 24.0;

/// Arrhenius activation energy of dye development, J / mol. FITTED by
/// tools/arrhenius_fit.py from ILFORD's time/temperature compensation chart
/// (40 cells, standard error 620 J/mol). That chart is for black-and-white
/// silver development; applying it to dye development is an ASSUMPTION
/// (see the script). verify.sh refuses a value the fit does not give.
constexpr double kDyeActivation = 67410.0;

/// Activation energy of the timing layer (the stop and the opacifier's
/// clearing), J / mol. ASSUMED: half the chart's dye figure, because it is a
/// diffusion through a polymer layer, not a chemical step. No source.
constexpr double kStopActivation = 0.5 * kDyeActivation;

/// Each dye layer's activation energy, J / mol: cyan, magenta, yellow.
/// Yellow keeps the chart's figure above. Cyan and magenta are FITTED by
/// tools/cast_fit.py to the manufacturer's DESCRIPTION of a cold print (a
/// green tint), not to any measurement: no published per-layer figure was
/// found. The fit asks, at kCastFitC, a mid grey on the colour stock to come
/// out with R = B (the hue exactly green) and with its dye balance off by as
/// much as v0.1.0's shared activation energy put it off (the same strength of
/// cast, turned from cyan to green). Rounded to 10 J/mol; verify.sh refuses a
/// value the fit does not give. The black-and-white stock has one image and
/// develops at the chart's figure, the process the chart is for.
constexpr double kLayerActivation[ 3 ] = { 103600.0, 106850.0, 67410.0 };
static_assert( kLayerActivation[ 2 ] == kDyeActivation, "yellow keeps the chart's figure" );
/// The cold temperature the cast fit is made at, degC: well below the
/// manufacturer's 13 degC, where the page describes the green tint.
constexpr double kCastFitC = 6.0;

/// Dye time constants at 24 degC, film seconds: cyan, magenta, yellow.
/// ASSUMED, not measured. Chosen so that the order is the one the process is
/// known for (the picture arrives blue-cyan and warms) and so that the print
/// is done in the manufacturer's stated 10-15 minutes: 3 tau of yellow is
/// 10 minutes. See ATTRIBUTIONS.md.
constexpr double kTauColour[ 3 ]  = { 70.0, 120.0, 200.0 };
/// The vintage stock develops slower and more evenly in the warm layers.
constexpr double kTauVintage[ 3 ] = { 90.0, 170.0, 230.0 };
/// The black-and-white stock has one image, so one tau.
constexpr double kTauMono         = 90.0;

/// The opacifier's clearing time constant, stop-seconds at 24 degC.
/// ASSUMED: with O0 below the picture is dark for about a minute and
/// readable by two, as a print is.
constexpr double kTauOpacifier = 45.0;

/// The stop dose: development ends when the timing layer has run this many
/// seconds at 24 degC. ASSUMED: 10 minutes, the short end of the stated
/// development time. At 24 degC the yellow layer reaches 1 - e^-3 = 95% of
/// its asymptote by then, and `Balance` below makes a neutral grey come out
/// neutral at exactly that point.
constexpr double kStopDose = 600.0;

/// The reagent front, in image heights per film-second from the pod edge.
/// ASSUMED: a camera ejects a print in about a second.
constexpr double kFrontSpeed = 1.0;

/// The dirty roller's circumference, as a fraction of the image height:
/// 5/16 of a 79 mm image is 24.7 mm, a roller of 7.9 mm diameter. ASSUMED.
/// 5/16 so that the period is a whole number of pixels at any raster whose
/// image height is a multiple of 16, and a known fraction otherwise; the
/// harness measures both.
constexpr double kRollerCircumference = 5.0 / 16.0;
/// Where along the spread the dirt first touches, in circumferences.
constexpr double kRollerPhase = 0.5;
/// The width of one mark, as a fraction of the image height.
constexpr double kRollerWidth = 0.018;
/// At full `Dirty Roller`, a mark takes this fraction of the dye.
constexpr double kRollerDepth = 0.6;

/// The opacifier's reflection densities, R G B: a dark green-grey.
constexpr float kOpacifier[ 3 ] = { 1.90f, 1.55f, 1.80f };

/// The reagent's white pigment, linear reflectance, R G B.
constexpr float kWhiteColour[ 3 ] = { 0.86f, 0.86f, 0.84f };
constexpr float kWhiteMono[ 3 ]   = { 0.85f, 0.85f, 0.86f };
/// What shows where the reagent never reached: the negative, dark blue-black.
constexpr float kUnreached[ 3 ] = { 0.030f, 0.036f, 0.058f };
/// The frame, linear reflectance: a slightly warm white plastic.
constexpr float kFrameColour[ 3 ] = { 0.80f, 0.79f, 0.75f };

/// The camera's meter. An integral camera meters the scene through its own
/// cell and sets the exposure; the lighten/darken wheel (`Exposure`) is an
/// offset from what it chose. Modelled as an AVERAGING meter over the image
/// window at the Take: the mean linear luminance of what the film will see
/// is brought to `kMeterTarget`, the gain held within the camera's range.
/// The target is the photographic mid grey; the range is ASSUMED (a real
/// camera's runs from a fast shutter in sun to seconds indoors, far wider,
/// but a clip that is almost all black should stay a dark print).
constexpr double kMeterTarget  = 0.18;
constexpr double kMeterMinStop = -2.0;
constexpr double kMeterMaxStop = 3.0;
/// The meter reads a 64 x 64 sampling of the window, averaged by its mip
/// chain to one texel at level 6.
constexpr int kMeterSize  = 64;
constexpr int kMeterLevel = 6;

/// The curve's knee sharpness, per log10 unit (rebate's and wetplate's).
constexpr double kKnee = 6.0;

/// A stock: the curve and the dye range per layer (C, M, Y).
struct Stock
{
	double shoulder;///< log10 H where the highlights reach Dmin
	double latitude;///< log10 H from the toe to the shoulder
	double dmin[ 3 ];
	double dmax[ 3 ];
	bool mono;
};
constexpr Stock kStocks[ 3 ] = {
	//Colour: a short latitude (four stops), Dmax 1.65 reflection.
	{ -0.30, 1.20, { 0.10, 0.10, 0.10 }, { 1.65, 1.65, 1.65 }, false },
	//Black & White: a longer latitude, a deeper black.
	{ -0.25, 1.50, { 0.06, 0.06, 0.06 }, { 1.90, 1.90, 1.90 }, true },
	//Vintage: shorter still, a warm base stain, weaker cyan.
	{ -0.35, 1.05, { 0.10, 0.16, 0.26 }, { 1.35, 1.50, 1.55 }, false },
};

/// Expired film, at `Expired` 1: Dmax falls by this fraction (cyan by the
/// second figure: the cyan dye fades first, so old prints go pink), the
/// base stains by this much density in magenta and yellow, and the paste,
/// dried, reaches this much less far.
constexpr double kExpiredDmaxLoss = 0.30;
constexpr double kExpiredCyanLoss = 0.45;
constexpr double kExpiredStain    = 0.14;
constexpr double kExpiredReach    = 0.30;

/// The spread's reach from the pod edge, in image heights, and how much
/// shorter the corners fall: reach( u ) = R ( 1 - k ( 2u - 1 )^2 ).
constexpr double kFullReach   = 1.25;
constexpr double kShortReach  = 1.04;
constexpr double kShortCorner = 0.28;

/// What a frame is worth in host seconds when the clock has not moved yet,
/// and the most one frame may add (a stalled host or a clip trigger).
constexpr double kNominalFrame  = 1.0 / 60.0;
constexpr double kMaxFrameDelta = 0.25;

/// Negative-control hooks. Always 0 in the plugin. Each perturbs the plugin's
/// own model so that `intest --negative` can show a check failing.
enum Perturb : int
{
	kPerturbSingleTau    = 1,  ///< every dye layer at magenta's tau: --order fails
	kPerturbNoFront      = 2,  ///< the reagent is everywhere at t = 0: --front fails
	kPerturbNoArrhenius  = 4,  ///< temperature changes no rate: --arrhenius fails
	kPerturbTauCyan      = 8,  ///< cyan's tau x 1.1 in the shader: --develop fails
	kPerturbRollerPeriod = 16, ///< the roller 2% larger than stated: --roller fails
	kPerturbResizeClears = 32, ///< a resize clears the print: --take fails
	kPerturbLiveCapture  = 64, ///< the capture follows the clip: --take fails
	kPerturbNoMeter      = 128,///< the meter is ignored: --meter fails
	kPerturbSharedActivation = 256,///< v0.1.0: every dye layer at kDyeActivation: --cast fails cold
};

/// Probe hooks for the harness, 0 in the plugin: 1 makes the print pass
/// write ( Dc, Dm, Dy, O_red ) as raw floats instead of the picture.
enum Probe : int
{
	kProbeNone      = 0,
	kProbeDensities = 1,
};

} // namespace instant::model
