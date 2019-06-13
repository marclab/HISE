/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace scriptnode
{

using namespace juce;
using namespace hise;



namespace core
{


}





namespace dynamics
{
Factory::Factory(DspNetwork* network) :
	NodeFactory(network)
{
	registerNode<gate>();
	registerNode<comp>();
	registerNode<limiter>();
	registerNode<envelope_follower>();
}

}



namespace core
{





}



namespace core
{
Factory::Factory(DspNetwork* network) :
	NodeFactory(network)
{
	registerPolyNode<seq, seq_poly>();
	registerPolyNode<ramp, ramp_poly>();
	registerNode<table>();
	registerNode<fix_delay>();
	registerPolyNode<oscillator, oscillator_poly>();
	registerPolyNode<ramp_envelope, ramp_envelope_poly>();
	registerPolyNode<gain, gain_poly>();
	registerNode<peak>();
	registerPolyNode<panner, panner_poly>();
	registerNode<tempo_sync>();
	registerPolyNode<timer, timer_poly>();
	registerPolyNode<midi, midi_poly>({});
	registerPolyNode<smoother, smoother_poly>({});
	registerPolyNode<haas, haas_poly>({});
}
}



}