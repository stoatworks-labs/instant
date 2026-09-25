#pragma once

#include <string>

/**
	The four passes, and the meter.

	1. **capture** -- picture size, RGBA16F, run only on a Take. The host's
	   frame, sRGB-decoded to linear and premultiplied onto black by its
	   alpha: the light that reached the film. It is kept, and the print
	   develops from it however the clip goes on to change. Then the
	   **meter**: the capture's luminance over the image window onto a 64 x 64
	   R32F grid, averaged by its mip chain -- the camera's averaging cell.

	2. **develop** -- picture size, RG32F, ping-ponged against its own
	   previous output, every frame a print exists. Per texel: where the
	   reagent front is (it left the pod edge at the take and moves at a
	   stated speed), and how much of this frame's film-seconds fell after
	   it arrived; that, times the Arrhenius rate at the current temperature,
	   is added to the dye dose (r) and the stop dose (g). Once the stop dose
	   passes the stop, no more dye moves.

	3. **resample** -- one buffer into a buffer of another size, bilinear.
	   Runs only on a resize, for the capture and the development state, so
	   that a raster change mid-development carries the print across instead
	   of clearing it.

	4. **print** -- to the host. The layout (the print, its frame, its square
	   image window, or the full frame at Border 0); the capture through the
	   stock's curve to each layer's asymptotic dye; the doses to the dye
	   delivered so far and the opacifier left; the spread's reach, lanes and
	   the dirty roller's marks; the white pigment seen through all of it;
	   the frame, a little texture and shine; sRGB, and Mix.

	The spread's geometry, the curve and the sRGB transfer are one GLSL
	library, `kModel`, compiled into the develop and print passes, preceded
	by a block of constants written from Model.h at assembly time, so each
	number exists once. `intest --dump-shaders DIR` writes out exactly the
	strings the plugin compiles, and that is what `tools/verify.sh` hands to
	glslc.
*/
namespace instant::shaders
{

std::string Vertex();
std::string Capture();
std::string Meter();
std::string Develop();
std::string Resample();
std::string Print();

} // namespace instant::shaders
