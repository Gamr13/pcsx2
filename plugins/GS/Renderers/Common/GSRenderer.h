/*
 *	Copyright (C) 2007-2009 Gabest
 *	http://www.gabest.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#pragma once

#include "../../GS.h"
#include "../../GSState.h"

class GSRenderer : public GSState
{
	bool Merge(int field);

protected:
	int m_dithering;
	int m_interlace;
	bool m_aa1;
	bool m_fxaa;
	bool m_texture_shuffle;
	GSVector2i m_real_size;

	virtual GSTexture* GetOutput(int i, int& y_offset) = 0;
	virtual GSTexture* GetFeedbackOutput() { return nullptr; }

public:
	GSDevice* m_dev;

public:
	GSRenderer();
	
	virtual void UpdateRendererOptions();

	virtual ~GSRenderer();
	virtual bool CreateDevice(GSDevice* dev);
	virtual void ResetDevice();
	virtual void VSync(int field);
	virtual bool CanUpscale() {return false;}
	virtual int GetUpscaleMultiplier() {return 1;}
	virtual GSVector2i GetCustomResolution() {return GSVector2i(0,0);}
	GSVector2i GetInternalResolution();

	void PurgePool();
};