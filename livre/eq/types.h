/**
 * Copyright (c) BBP/EPFL 2005-2016 Ahmet.Bilgili@epfl.ch
 *
 * This file is part of Livre <https://github.com/BlueBrain/Livre>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _livreEqTypes_h_
#define _livreEqTypes_h_

#include <livre/lib/types.h>

#include <eq/types.h>

#include <lunchbox/refPtr.h>

namespace lexis
{
namespace render
{
class ImageJPEG;
}
}

namespace livre
{

class CameraSettings;
class Channel;
class Client;
class Config;
class FrameData;
class FrameSettings;
class LocalInitData;
class Node;
class Pipe;
class RayCastRenderer;
class RenderSettings;
class VolumeSettings;
class Window;

}

#endif // _livreEqTypes_h_
