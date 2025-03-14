// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/FrameNumber.h"
#include "Curves/KeyHandle.h"
#include "CurveDataAbstraction.h"
#include "CurveModel.h"

struct FMovieSceneFloatChannel;
struct FMovieSceneDoubleChannel;
class UControlRig;
class ISequencer;
struct FRigControl;
class UMovieSceneSection;
class FCurveEditor;

/*
* Contains the selection state for a set of keys to blend with the anim slider
*/
struct FAnimSliderKeySelection
{
	//list of keys in a row
	struct FContiguousKeys
	{
		int32 PreviousIndex;
		TArray <int32> Indices;
		int32 NextIndex;
		FContiguousKeys(const TArray<FKeyHandle>& InAllKeyHandles, const TArray<FKeyPosition>& InAllKeyPositions, const TArray<int32>& ContiguousKeyIndices);
	};

	//all the selectedv keys in a row for a particular curve, plust pointers to all of the keys
	struct FContiguousKeysArray
	{
		TArray<FKeyHandle> AllKeyHandles;
		TArray<FKeyPosition> AllKeyPositions;

		TArray<FContiguousKeys> KeysArray;

		FContiguousKeysArray(const TArray<FKeyHandle>& InAllKeyHandle, const TArray<FKeyPosition>& InAllKeyPositions);
		void Add(const TArray<int32>& ContiguousKeyIndices);
	};

	bool Setup(const TWeakPtr<ISequencer>& InSequencer);

	TSharedPtr<FCurveEditor> CurveEditor;
	TMap<FCurveModelID, FContiguousKeysArray> KeyMap;
private:
	void GetMapOfContiguousKeys();

};

/*
*  Key bounds and their values used to blend with Controls and Actor float channels
*/
struct FChannelKeyBounds
{
	FChannelKeyBounds() : bValid(false), FloatChannel(nullptr), DoubleChannel(nullptr), PreviousIndex(INDEX_NONE), NextIndex(INDEX_NONE), PreviousFrame(0), NextFrame(0),
		CurrentFrame(0), PreviousValue(0.0), NextValue(0.0), CurrentValue(0.0) {}
	bool bValid;
	FMovieSceneFloatChannel* FloatChannel;
	FMovieSceneDoubleChannel* DoubleChannel;
	int32 PreviousIndex;
	int32 NextIndex;
	FFrameNumber PreviousFrame;
	FFrameNumber NextFrame;
	FFrameNumber CurrentFrame;
	double PreviousValue;
	double NextValue;
	double CurrentValue;
};

/*
* Contains the selection state for a set of Control Rig Controls to blend with the anim slider
*/
struct FAnimSliderObjectSelection
{
	//set of possible float channels
	struct FObjectChannels
	{
		FObjectChannels() : Section(nullptr) {};
		TArray<FChannelKeyBounds>  KeyBounds;
		UMovieSceneSection* Section;
	};

	bool Setup(TWeakPtr<ISequencer>& InSequencer, TWeakPtr<class FControlRigEditMode>& InEditMode);
	bool Setup(const TArray<UControlRig*>& SelectedControlRigs, TWeakPtr<ISequencer>& InSequencer);

	TArray<FObjectChannels>  ChannelsArray;

private:

	//float or double will be non-null
	void SetupChannel(FFrameNumber CurrentFrame, TArray<FFrameNumber>& KeyTimes, TArray<FKeyHandle>& Handles, FMovieSceneFloatChannel* FloatChannel,
		FMovieSceneDoubleChannel* DoubleChannel, FChannelKeyBounds& KeyBounds);

	TArray<UControlRig*> GetControlRigs(TWeakPtr<class FControlRigEditMode>& InEditMode);
};

/*
* Base struct that performs common actions for slider animation tools
*/
struct FBaseAnimSlider
{
	FBaseAnimSlider(){};
	virtual ~FBaseAnimSlider() {};
	/**
	 Setup up what's needed for the slider to blend
	* @param InSequencer Sequencer to get keys, controls, etc. from
	* @param returns true if we have something that we can blend
	*/
	virtual bool Setup(TWeakPtr<ISequencer>& InSequencer, TWeakPtr<FControlRigEditMode>& InEditMode);

	/**
	* @param InSequencer Sequencer to blend at current time
	* @param InSequencer BlendValue where in time to Blend in at Current Frame, 0.0 will be at current frame, -1.0 will value at previous key, 1.0 value at next key,
	* other values interpolated between.
	*/
	virtual bool Blend(TWeakPtr<ISequencer>& InSequencer, const double BlendValue) = 0;

	/**
	* @return the name of the slider
	*/
	virtual FText GetText() const = 0;

	/**
	* @return the tooltip text for the slider
	*/
	virtual FText GetTooltipText() const = 0;

protected:

	FAnimSliderKeySelection  KeySelection;
	FAnimSliderObjectSelection ObjectSelection;
};

/*
* Simple manager that holds all of the registered blend tools
* This will then be used by whatever the view wants to do to display these tool
*/
class FAnimBlendToolManager
{
public:
	FAnimBlendToolManager() {};
	void RegisterAnimSlider(TSharedPtr<FBaseAnimSlider> AnimSlider)
	{
		AnimSliders.Add(AnimSlider);
	}
	const TArray<TSharedPtr<FBaseAnimSlider>>& GetAnimSliders() const 
	{ 
		return AnimSliders; 
	}
	TArray<TSharedPtr<FBaseAnimSlider>>& GetAnimSliders() 
	{
		return AnimSliders;
	}
private:
	TArray<TSharedPtr<FBaseAnimSlider>> AnimSliders;
};

/**
*
*  Struct containing data that is used by Blending
*
**/
struct FBlendStruct
{
	//Previous and Next are the keys that are before/after the contiguous set of keys, 
	//to get the Previous key for example you use AllKeyPostions[Indices[CurrentIndex -1]]
	double PreviousTime; 
	double PreviousValue; 
	double CurrentTime; 
	double CurrentValue;
	double NextTime;
	double NextValue;
	double BlendValue;
	double FirstValue;
	double LastValue;
	TArray<FKeyPosition> AllKeyPositions;
	TArray <int32> Indices;
	int32 CurrentIndex; 

	FBlendStruct(const TArray<FKeyPosition>& InAllKeyPositions, const TArray<int32>& InIndices) 
		: AllKeyPositions(InAllKeyPositions), Indices(InIndices) {};
	void SetValues(double InPreviousTime, double InPreviousValue, double InCurrentTime, double InCurrentValue,
		double InNextTime, double InNextValue, double InBlendValue, double InFirstValue, double InLastValue,
		int32 InCurrentIndex)
	{
		PreviousTime = InPreviousTime;
		PreviousValue = InPreviousValue;
		CurrentTime = InCurrentTime;
		CurrentValue = InCurrentValue;
		NextTime = InNextTime;
		NextValue = InNextValue;
		BlendValue = InBlendValue;
		FirstValue = InFirstValue;
		LastValue = InLastValue;
		CurrentIndex = InCurrentIndex;
	}

private:
	FBlendStruct() = delete;
	
};
/**
*
*  Slider that used the next and previous values to do the blend
* 
**/
struct FBasicBlendSlider : public FBaseAnimSlider
{
	virtual double DoBlend(const FBlendStruct& BlendStruct) = 0;
	//BaseAnimSlider overrides
	virtual bool Blend(TWeakPtr<ISequencer>& InSequencer, const double BlendValue) override;
};

/**
*
*    Classic tween slider, will move to the blend between the next and previous keys
*
**/
struct FControlsToTween :public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const override;
	virtual FText GetTooltipText() const override;
	virtual bool Setup(TWeakPtr<ISequencer>& InSequencer, TWeakPtr<FControlRigEditMode>& InEditMode) override;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;

	bool Setup(const TArray<UControlRig*>& SelectedControlRigs, TWeakPtr<ISequencer>& InSequencer);

};

/**
*
*   Slider that will amplify or reduce the differences from the slope between the next or previous
*
**/
struct FPushPullSlider :public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const;
	virtual FText GetTooltipText() const;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;
};

/**
*
*   Similar to tween but will blend off of the current position when blending to the next or previous keys
*
**/
struct FBlendNeighborSlider : public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const;
	virtual FText GetTooltipText() const;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;
};


/**
*
*   Will offset keys from previous or next values
*
**/
struct FBlendRelativeSlider: public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const;
	virtual FText GetTooltipText() const;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;
};


/**
*
*   Will do an eased blend
*
**/
struct FBlendToEaseSlider : public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const;
	virtual FText GetTooltipText() const;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;
};


/**
*
*   Will do a smooth or rough blend
*
**/
struct FSmoothRoughSlider : public FBasicBlendSlider
{
	//BaseAnimSlider overrides
	virtual FText GetText() const;
	virtual FText GetTooltipText() const;

	//BasicBlendSlider overrides
	virtual double DoBlend(const FBlendStruct& BlendStruct) override;
};

