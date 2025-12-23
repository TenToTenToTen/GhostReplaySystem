/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "QuantizationHelper.h"
#include "BloodStainFileUtils.h"
#include "BloodStainFileOptions.h"
#include "QuantizationTypes.h"

namespace BloodStainFileUtils_Internal
{

void ComputeRanges(FRecordSaveData& SaveData)
{
    for (FRecordActorSaveData& ActorData : SaveData.RecordActorDataArray)
    {
        ActorData.BoneRanges.Empty();
        ActorData.BoneScaleRanges.Empty();
        ActorData.ComponentRanges = FLocRange();
        bool bIsComponentRangeInitialized  = false;
        bool bIsComponentScaleRangeInitialized = false; 
        for (const FRecordFrame& Frame : ActorData.RecordedFrames)
        {
            for (const auto& Pair : Frame.SkeletalMeshBoneTransforms)
            {
                const FString& BoneKey = Pair.Key;
                const FBoneComponentSpace& Space = Pair.Value;
                FLocRange& R = ActorData.BoneRanges.FindOrAdd(BoneKey);
                FScaleRange& ScaleRange = ActorData.BoneScaleRanges.FindOrAdd(BoneKey);

                bool bIsFirst = (R.PosMin.IsNearlyZero() && R.PosMax.IsNearlyZero());
                if (bIsFirst && Space.BoneTransforms.Num() > 0)
                {
                    R.PosMin = R.PosMax = Space.BoneTransforms[0].GetLocation();
                }

                for (const FTransform& BoneT : Space.BoneTransforms)
                {
                    const FVector Loc = BoneT.GetLocation();
                    R.PosMin = R.PosMin.ComponentMin(Loc);
                    R.PosMax = R.PosMax.ComponentMax(Loc);
                }
                
                bool bIsFirstScale = (ScaleRange.ScaleMin.IsNearlyZero() && ScaleRange.ScaleMax.IsNearlyZero());

                if (bIsFirstScale && Space.BoneTransforms.Num() > 0)
                {
                    ScaleRange.ScaleMin = ScaleRange.ScaleMax = Space.BoneTransforms[0].GetScale3D();
                }

                for (const FTransform& BoneT : Space.BoneTransforms)
                {
                    const FVector Scale = BoneT.GetScale3D();
                    ScaleRange.ScaleMin = ScaleRange.ScaleMin.ComponentMin(Scale);
                    ScaleRange.ScaleMax = ScaleRange.ScaleMax.ComponentMax(Scale);
                }
            }
            
            for (const auto& Pair : Frame.RelativeTransforms)
            {
                const FTransform& ComponentT = Pair.Value;
                const FVector Loc = ComponentT.GetLocation();
                const FVector Scale = ComponentT.GetScale3D();

                if (!bIsComponentRangeInitialized)
                {
                    ActorData.ComponentRanges.PosMin = ActorData.ComponentRanges.PosMax = Loc;
                    bIsComponentRangeInitialized = true;
                }
                else
                {
                    ActorData.ComponentRanges.PosMin = ActorData.ComponentRanges.PosMin.ComponentMin(Loc);
                    ActorData.ComponentRanges.PosMax = ActorData.ComponentRanges.PosMax.ComponentMax(Loc);
                }

                if (!bIsComponentScaleRangeInitialized)
                {
                    ActorData.ComponentScaleRanges.ScaleMin = ActorData.ComponentScaleRanges.ScaleMax = Scale;
                    bIsComponentScaleRangeInitialized = true;
                }
                else
                {
                    ActorData.ComponentScaleRanges.ScaleMin = ActorData.ComponentScaleRanges.ScaleMin.ComponentMin(Scale);
                    ActorData.ComponentScaleRanges.ScaleMax = ActorData.ComponentScaleRanges.ScaleMax.ComponentMax(Scale);
                }
            }
        }
    }
    
}
void SerializeQuantizedTransform(FArchive& Ar, const FTransform& Transform, const ETransformQuantizationMethod& QuantOpts, const FLocRange* LocRange, const FScaleRange* ScaleRange)
{
    switch (QuantOpts)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q(Transform);
            Ar << Q;
        }
        break;
    case ETransformQuantizationMethod::Standard_Medium:
        {
            FQuantizedTransform_Compact Q(Transform);
            Ar << Q;
        }
        break;
    case ETransformQuantizationMethod::Standard_Low:
        {
            /** Only use Location / Scale Range if Quantization Option is Standard_Low */
            FQuantizedTransform_Lowest Q(Transform, *LocRange, *ScaleRange);
            Ar << Q;
            break;
        }
    case ETransformQuantizationMethod::None:
    default:
        {
            /** Basic Serialization for FTransform By Default */
            FTransform NonConstT = Transform;
            Ar << NonConstT;
        }
    }
}

FTransform DeserializeQuantizedTransform(FArchive& Ar, const ETransformQuantizationMethod& Opts, const FLocRange* LocRange, const FScaleRange* ScaleRange)
{
    switch (Opts)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::Standard_Medium:
        {
            FQuantizedTransform_Compact Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::Standard_Low:
        {
            FQuantizedTransform_Lowest Q;
            Ar << Q;
            return Q.ToTransform(*LocRange, *ScaleRange);
        }
    default:
        {
            FTransform T;
            Ar << T;
            return T;
        }
    }
}

void SerializeSaveData(FArchive& RawAr, FRecordSaveData& SaveData, ETransformQuantizationMethod& QuantOpts)
{
    ComputeRanges(SaveData);

    int32 NumActors = SaveData.RecordActorDataArray.Num();
    RawAr << NumActors;

    for (FRecordActorSaveData& ActorData : SaveData.RecordActorDataArray)
    {
        RawAr << ActorData.PrimaryComponentName;
        RawAr << ActorData.ComponentIntervals;
        RawAr << ActorData.ComponentRanges;
        RawAr << ActorData.ComponentScaleRanges;
        RawAr << ActorData.BoneRanges;
        RawAr << ActorData.BoneScaleRanges;        

        int32 NumFrames = ActorData.RecordedFrames.Num();
        RawAr << NumFrames;

        for (FRecordFrame& Frame : ActorData.RecordedFrames)
        {
            RawAr << Frame.TimeStamp;
            RawAr << Frame.FrameIndex;

            // Component's Local Transforms
            int32 NumComps = Frame.RelativeTransforms.Num();
            RawAr << NumComps;
            for (auto& Pair : Frame.RelativeTransforms)
            {
                RawAr << Pair.Key;

                const FLocRange* Range = &ActorData.ComponentRanges;
                const FScaleRange* ScaleRange = &ActorData.ComponentScaleRanges;
                if (ensure(Range && ScaleRange))
                {
                    SerializeQuantizedTransform(RawAr, Pair.Value, QuantOpts, Range, ScaleRange);
                }
            }

            // Skeletal Mesh Component's BoneTransforms
            int32 NumBoneMaps = Frame.SkeletalMeshBoneTransforms.Num();
            RawAr << NumBoneMaps;
            for (auto& BonePair : Frame.SkeletalMeshBoneTransforms)
            {
                RawAr << BonePair.Key;

                const FBoneComponentSpace& Space = BonePair.Value;
                int32 BoneCount = Space.BoneTransforms.Num();
                RawAr << BoneCount;

                const FLocRange* Range = ActorData.BoneRanges.Find(BonePair.Key);
                const FScaleRange* ScaleRange = ActorData.BoneScaleRanges.Find(BonePair.Key);
                if (ensure(Range && ScaleRange))
                {
                    for (const FTransform& BoneT : Space.BoneTransforms)
                    {
                        SerializeQuantizedTransform(RawAr, BoneT, QuantOpts, Range, ScaleRange);
                    }
                }
            }
        }
    }
    
}

void DeserializeSaveData(FArchive& DataAr, FRecordSaveData& OutData, const ETransformQuantizationMethod& QuantOpts)
{
    int32 NumActors = 0;
    DataAr << NumActors;
    OutData.RecordActorDataArray.Empty(NumActors);

    for (int32 i = 0; i < NumActors; ++i)
    {
        FRecordActorSaveData ActorData;
        DataAr << ActorData.PrimaryComponentName;
        DataAr << ActorData.ComponentIntervals;
        DataAr << ActorData.ComponentRanges;
        DataAr << ActorData.ComponentScaleRanges;
        DataAr << ActorData.BoneRanges;
        DataAr << ActorData.BoneScaleRanges;

        int32 NumFrames = 0;
        DataAr << NumFrames;
        ActorData.RecordedFrames.Empty(NumFrames);

        for (int32 f = 0; f < NumFrames; ++f)
        {
            FRecordFrame Frame;
            DataAr << Frame.TimeStamp;
            DataAr << Frame.FrameIndex; 

            // Component's Local Transforms
            int32 NumComps = 0;
            DataAr << NumComps;
            for (int32 c = 0; c < NumComps; ++c)
            {
                FString Key;
                DataAr << Key;
                const FLocRange* Range = &ActorData.ComponentRanges;
                const FScaleRange* ScaleRange = &ActorData.ComponentScaleRanges;
                FTransform T = DeserializeQuantizedTransform(DataAr, QuantOpts, Range, ScaleRange);
                Frame.RelativeTransforms.Add(Key, T);
            }

            // Skeletal Mesh Component's Bone Transforms
            int32 NumBoneMaps = 0;
            DataAr << NumBoneMaps;
            for (int32 bm = 0; bm < NumBoneMaps; ++bm)
            {
                FString Key;
                int32 BoneCount = 0;
                
                DataAr << Key;                
                DataAr << BoneCount;
                
                FBoneComponentSpace Space;
                Space.BoneTransforms.Empty(BoneCount);
                
                const FLocRange* Range = ActorData.BoneRanges.Find(Key);
                const FScaleRange* ScaleRange = ActorData.BoneScaleRanges.Find(Key);
                
                for (int32 b = 0; b < BoneCount; ++b)
                {
                    FTransform BoneT = DeserializeQuantizedTransform(DataAr, QuantOpts, Range, ScaleRange);
                    Space.BoneTransforms.Add(BoneT);
                }
                Frame.SkeletalMeshBoneTransforms.Add(Key, Space);
            }

            ActorData.RecordedFrames.Add(Frame);
        }

        OutData.RecordActorDataArray.Add(ActorData);
    }
}

} // namespace BloodStainFileUtils_Internal
