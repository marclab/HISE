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
 *   which also must be licensed for commercial applications:
 *
 *   http://www.juce.com
 *
 *   ===========================================================================
 */

namespace scriptnode
{
using namespace juce;
using namespace hise;

ModulationSourceNode::ModulationTarget::ModulationTarget(ModulationSourceNode* parent_, ValueTree data_) :
	ConnectionBase(parent_->getScriptProcessor(), data_),
	parent(parent_),
	active(data, PropertyIds::Enabled, parent->getUndoManager(), true)
{
	rangeUpdater.setCallback(data, RangeHelpers::getRangeIds(),
		valuetree::AsyncMode::Coallescated,
		[this](Identifier, var)
	{
		inverted = RangeHelpers::checkInversion(data, &rangeUpdater, parent->getUndoManager());
		connectionRange = RangeHelpers::getDoubleRange(data);
	});

	
	initRemoveUpdater(parent);

	expressionUpdater.setCallback(data, { PropertyIds::Expression },
		valuetree::AsyncMode::Synchronously,
		[this](Identifier, var newValue)
	{
		setExpression(newValue.toString());
	});

	callback = nothing;
}



ModulationSourceNode::ModulationTarget::~ModulationTarget()
{
	
}

bool ModulationSourceNode::ModulationTarget::findTarget()
{
	String nodeId = data[PropertyIds::NodeId];
	String parameterId = data[PropertyIds::ParameterId];

	auto list = parent->getRootNetwork()->getListOfNodesWithType<NodeBase>(true);

	for (auto n : list)
	{
		if (n->getId() == nodeId)
		{
			auto enabled = n->getRootNetwork()->isInSignalPath(n);

			if(!enabled && !n->isBeingMoved())
				data.setProperty(PropertyIds::Enabled, false, parent->getUndoManager());

			for (int i = 0; i < n->getNumParameters(); i++)
			{
				if (n->getParameter(i)->getId() == parameterId)
				{
					targetParameter = n->getParameter(i);
					callback = targetParameter->getCallback();

#if 0
					removeWatcher.setCallback(n->getValueTree(),
						valuetree::AsyncMode::Synchronously, true,
						[this](ValueTree&)
					{
						data.getParent().removeChild(data, parent->getUndoManager());
					});
#endif



					return true;
				}
			}
		}
	}

	return false;
}

void ModulationSourceNode::ModulationTarget::setExpression(const String& exprCode)
{
#if HISE_INCLUDE_SNEX
	snex::JitExpression::Ptr newExpr;

	if (exprCode.isEmpty())
	{
		SpinLock::ScopedLockType lock(expressionLock);
		std::swap(expr, newExpr);
	}
	else
	{
		newExpr = new snex::JitExpression(exprCode, parent);

		if (newExpr->isValid())
		{
			SpinLock::ScopedLockType lock(expressionLock);
			std::swap(newExpr, expr);
		}
	}
#endif
}

void ModulationSourceNode::ModulationTarget::applyValue(double value)
{
	if (!active)
		return;

	switch (opType)
	{
	case SetValue: targetParameter->setValueAndStoreAsync(value); break;
	case Multiply: targetParameter->multiplyModulationValue(value); break;
	case Add:	   targetParameter->addModulationValue(value); break;
        default:   break;
	}
}

juce::ValueTree ModulationSourceNode::getModulationTargetTree()
{
	auto vt = getValueTree().getChildWithName(PropertyIds::ModulationTargets);

	if (!vt.isValid())
	{
		vt = ValueTree(PropertyIds::ModulationTargets);
		getValueTree().addChild(vt, -1, getUndoManager());
	}

	return vt;
}

ModulationSourceNode::ModulationSourceNode(DspNetwork* n, ValueTree d) :
	NodeBase(n, d, 0)
{
	targetListener.setCallback(getModulationTargetTree(),
		valuetree::AsyncMode::Synchronously,
		[this](ValueTree c, bool wasAdded)
	{
		if (wasAdded)
		{
			ValueTree newTree(c);
			targets.add(new ModulationTarget(this, newTree));
		}
		else
		{
			for (auto t : targets)
			{
				if (t->data == c)
				{
					if (t->targetParameter != nullptr)
						t->targetParameter->data.removeProperty(PropertyIds::ModulationTarget, getUndoManager());

					targets.removeObject(t);
					break;
				}
			}
		}

		checkTargets();
	});
}

var ModulationSourceNode::addModulationTarget(NodeBase::Parameter* n)
{
	for (auto t : targets)
	{
		if (t->targetParameter.get() == n)
			return var(t);
	}

	ValueTree m(PropertyIds::ModulationTarget);

	m.setProperty(PropertyIds::NodeId, n->parent->getId(), nullptr);
	m.setProperty(PropertyIds::ParameterId, n->getId(), nullptr);
	m.setProperty(PropertyIds::OpType, OperatorIds::SetValue.toString(), nullptr);
	m.setProperty(PropertyIds::Enabled, true, nullptr);

	n->data.setProperty(PropertyIds::ModulationTarget, true, getUndoManager());

	auto range = RangeHelpers::getDoubleRange(n->data);

	RangeHelpers::storeDoubleRange(m, false, range, nullptr);

	m.setProperty(PropertyIds::Expression, "", nullptr);

	getModulationTargetTree().addChild(m, -1, getUndoManager());

	return var(targets.getLast());
}

juce::String ModulationSourceNode::createCppClass(bool isOuterClass)
{
	auto s = NodeBase::createCppClass(isOuterClass);

	if (getModulationTargetTree().getNumChildren() > 0)
		return CppGen::Emitter::wrapIntoTemplate(s, "wrap::mod");
	else
		return s;
}

void ModulationSourceNode::prepare(PrepareSpecs ps)
{
	ringBuffer = new SimpleRingBuffer();

	if (ps.sampleRate > 0.0)
		sampleRateFactor = 32768.0 / ps.sampleRate;

	prepareWasCalled = true;

	checkTargets();
}

void ModulationSourceNode::sendValueToTargets(double value, int numSamplesForAnalysis)
{
	if (ringBuffer != nullptr && 
		numSamplesForAnalysis > 0 &&
		getRootNetwork()->isRenderingFirstVoice())
	{
		ringBuffer->write(value, (int)(jmax(1.0, sampleRateFactor * (double)numSamplesForAnalysis)));
	}

	if (scaleModulationValue)
	{
		value = jlimit(0.0, 1.0, value);

		for (auto t : targets)
		{
#if HISE_INCLUDE_SNEX
			SpinLock::ScopedLockType l(t->expressionLock);

			if (t->expr != nullptr)
			{
				auto thisValue = t->expr->getValue(value);
				t->applyValue(thisValue);
                continue;
			}
#endif
			
            auto thisValue = value;
            
            if (t->inverted)
                thisValue = 1.0 - thisValue;
            
            auto normalised = t->connectionRange.convertFrom0to1(thisValue);
            
            t->applyValue(normalised);
		}
	}
	else
	{
		for (auto t : targets)
		{
#if HISE_INCLUDE_SNEX
			SpinLock::ScopedLockType l(t->expressionLock);

			if (t->expr != nullptr)
			{
				auto thisValue = t->expr->getValue(value);
				t->applyValue(thisValue);
				continue;
			}
#endif

			t->applyValue(value);
		}
	}

	lastModValue = value;
}

void ModulationSourceNode::logMessage(const String& s)
{
#if USE_BACKEND
	auto p = dynamic_cast<Processor*>(getScriptProcessor());

	debugToConsole(p, s);
#else
    DBG(s);
    ignoreUnused(s);
#endif
}

int ModulationSourceNode::fillAnalysisBuffer(AudioSampleBuffer& b)
{
	if (ringBuffer != nullptr)
		return ringBuffer->read(b);

	return 0;
}

void ModulationSourceNode::setScaleModulationValue(bool shouldScaleValue)
{
	scaleModulationValue = shouldScaleValue;
}

void ModulationSourceNode::checkTargets()
{
	// We need to skip this if the node wasn't initialised properly
	if (!prepareWasCalled)
		return;

	for (int i = 0; i < targets.size(); i++)
	{
		if (!targets[i]->findTarget())
			targets.remove(i--);
	}
}

ModulationSourceNode::SimpleRingBuffer::SimpleRingBuffer()
{
	clear();
}

void ModulationSourceNode::SimpleRingBuffer::clear()
{
	memset(buffer, 0, sizeof(float) * RingBufferSize);
}

int ModulationSourceNode::SimpleRingBuffer::read(AudioSampleBuffer& b)
{
	while (isBeingWritten) // busy wait, but OK
	{
		return 0;
	}

	jassert(b.getNumSamples() == RingBufferSize && b.getNumChannels() == 1);

	auto dst = b.getWritePointer(0);
	int thisWriteIndex = writeIndex;
	int numBeforeIndex = thisWriteIndex;
	int offsetBeforeIndex = RingBufferSize - numBeforeIndex;

	FloatVectorOperations::copy(dst + offsetBeforeIndex, buffer, numBeforeIndex);
	FloatVectorOperations::copy(dst, buffer + thisWriteIndex, offsetBeforeIndex);

	int numToReturn = numAvailable;
	numAvailable = 0;
	return numToReturn;
}

void ModulationSourceNode::SimpleRingBuffer::write(double value, int numSamples)
{
	ScopedValueSetter<bool> svs(isBeingWritten, true);

	if (numSamples == 1)
	{
		buffer[writeIndex++] = (float)value;

		if (writeIndex >= RingBufferSize)
			writeIndex = 0;

		numAvailable++;
		return;
	}
	else
	{
		int numBeforeWrap = jmin(numSamples, RingBufferSize - writeIndex);
		int numAfterWrap = numSamples - numBeforeWrap;

		if (numBeforeWrap > 0)
			FloatVectorOperations::fill(buffer + writeIndex, (float)value, numBeforeWrap);

		writeIndex += numBeforeWrap;

		if (numAfterWrap > 0)
		{
			FloatVectorOperations::fill(buffer, (float)value, numAfterWrap);
			writeIndex = numAfterWrap;
		}

		numAvailable += numSamples;
	}
}

ModulationSourceBaseComponent::ModulationSourceBaseComponent(PooledUIUpdater* updater) :
	SimpleTimer(updater)
{

}

void ModulationSourceBaseComponent::paint(Graphics& g)
{
	g.setColour(Colours::white.withAlpha(0.1f));
	g.drawRect(getLocalBounds(), 1);
	g.setColour(Colours::white.withAlpha(0.1f));
	g.setFont(GLOBAL_BOLD_FONT());
	g.drawText("Drag to modulation target", getLocalBounds().toFloat(), Justification::centred);
}

juce::Image ModulationSourceBaseComponent::createDragImage()
{
	Image img(Image::ARGB, 100, 60, true);
	Graphics g(img);

	g.setColour(Colour(SIGNAL_COLOUR).withAlpha(0.4f));
	g.fillAll();
	g.setColour(Colours::black);
	g.setFont(GLOBAL_BOLD_FONT());

	g.drawText(getSourceNodeFromParent()->getId(), 0, 0, 128, 48, Justification::centred);

	return img;
}

void ModulationSourceBaseComponent::mouseDrag(const MouseEvent&)
{
	if (getSourceNodeFromParent() == nullptr)
		return;

	if (auto container = dynamic_cast<DragAndDropContainer*>(findParentComponentOfClass<DspNetworkGraph>()->root.get()))
	{
		// We need to be able to drag it anywhere...
		//while (auto pc = DragAndDropContainer::findParentDragContainerFor(dynamic_cast<Component*>(container)))
//			container = pc;

		var d;

		DynamicObject::Ptr details = new DynamicObject();

		details->setProperty(PropertyIds::ID, sourceNode->getId());
		details->setProperty(PropertyIds::ModulationTarget, true);

		container->startDragging(var(details), this, createDragImage());
	}
}


void ModulationSourceBaseComponent::mouseDown(const MouseEvent& e)
{
	if (getSourceNodeFromParent() == nullptr)
		return;

	if (e.mods.isRightButtonDown())
	{
		auto pe = new MacroPropertyEditor(sourceNode, sourceNode->getValueTree(), PropertyIds::ModulationTargets);

		pe->setName("Edit Modulation Targets");
        
        
        auto g = findParentComponentOfClass<DspNetworkGraph::ScrollableParent>();
        auto b = g->getLocalArea(this, getLocalBounds());
        
		g->setCurrentModalWindow(pe, b);

	}
}

scriptnode::ModulationSourceNode* ModulationSourceBaseComponent::getSourceNodeFromParent() const
{
	if (sourceNode == nullptr)
	{
		if (auto pc = findParentComponentOfClass<NodeComponent>())
		{
			sourceNode = dynamic_cast<ModulationSourceNode*>(pc->node.get());
		}
	}

	return sourceNode;
}


ModulationSourcePlotter::ModulationSourcePlotter(PooledUIUpdater* updater) :
	ModulationSourceBaseComponent(updater)
{
	setOpaque(true);
	setSize(0, ModulationSourceNode::ModulationBarHeight);
	buffer.setSize(1, ModulationSourceNode::RingBufferSize);
}

void ModulationSourcePlotter::timerCallback()
{
	skip = !skip;

	if (skip)
		return;

	if (getSourceNodeFromParent() != nullptr)
	{
		jassert(getSamplesPerPixel() > 0);

		auto numNew = sourceNode->fillAnalysisBuffer(buffer);

		pixelCounter += (float)numNew / (float)getSamplesPerPixel();

		

		rebuildPath();

		if (pixelCounter > 4.0f)
		{
			pixelCounter = fmod(pixelCounter, 4.0f);
			repaint();
		}	
	}
}

void ModulationSourcePlotter::rebuildPath()
{
	float offset = 2.0f;

	float rectangleWidth = 1.0f;

	auto width = (float)getWidth() - 2.0f * offset;
	auto maxHeight = (float)getHeight() - 2.0f * offset;

	int samplesPerPixel = getSamplesPerPixel();

	rectangles.clear();

	int sampleIndex = 0;

	for (float i = 0.0f; i < width; i += rectangleWidth)
	{
		float maxValue = jlimit(0.0f, 1.0f, buffer.getMagnitude(0, sampleIndex, samplesPerPixel));
		FloatSanitizers::sanitizeFloatNumber(maxValue);
		float height = maxValue * maxHeight;
		float y = offset + maxHeight - height;

		sampleIndex += samplesPerPixel;

		rectangles.add({ i + offset, y, rectangleWidth, height });
	}
}

void ModulationSourcePlotter::paint(Graphics& g)
{
	g.fillAll(Colour(0xFF333333));

	g.setColour(Colour(0xFF999999));
	g.fillRectList(rectangles);

	ModulationSourceBaseComponent::paint(g);
}

int ModulationSourcePlotter::getSamplesPerPixel() const
{
	float offset = 2.0f;

	float rectangleWidth = 1.0f;

	auto width = (float)getWidth() - 2.0f * offset;

	int samplesPerPixel = ModulationSourceNode::RingBufferSize / jmax((int)(width / rectangleWidth), 1);
	return samplesPerPixel;
}

}

