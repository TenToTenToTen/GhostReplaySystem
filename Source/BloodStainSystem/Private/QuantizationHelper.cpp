#include "QuantizationHelper.h"
#include "BloodStainFileUtils.h"    // FRecordSaveData 등 정의
#include "BloodStainFileOptions.h"  // FQuantizationOptions
#include "QuantizationTypes.h"

namespace BloodStainFileUtils_Internal
{

void ComputeBoneRanges(FRecordSaveData& SaveData)
{
    for (FRecordActorSaveData& ActorData : SaveData.RecordActorDataArray)
    {
        ActorData.BoneRanges.Empty();
        for (const FRecordFrame& Frame : ActorData.RecordedFrames)
        {
            for (const auto& Pair : Frame.SkeletalMeshBoneTransforms)
            {
                const FString& BoneKey = Pair.Key;
                const FBoneComponentSpace& Space = Pair.Value;
                FBoneRange& R = ActorData.BoneRanges.FindOrAdd(BoneKey);

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
            }
        }
    }
    
}


void SerializeQuantizedTransform(FArchive& Ar, const FTransform& Transform, const FQuantizationOption& QuantOpts, const FBoneRange* Range)
{
    switch (QuantOpts.Method)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q(Transform);
            Ar << Q;
        }
        break;
    case ETransformQuantizationMethod::Standard_Compact:
        {
            FQuantizedTransform_Compact Q(Transform);
            Ar << Q;
        }
        break;
    case ETransformQuantizationMethod::Standard_Low:
        {
            // Range 유무에 따라 다른 생성자 호출
            if (Range)
            {
                FQuantizedTransform_Lowest Q(Transform, *Range);
                Ar << Q;
            }
            else
            {
                FQuantizedTransform_Lowest Q(Transform);
                Ar << Q;
            }
            break;
        }
    case ETransformQuantizationMethod::None:
    default:
        {
            // 기본 FTransform 직렬화
            Ar << const_cast<FTransform&>(Transform);
        }
    }
}

FTransform DeserializeQuantizedTransform(FArchive& Ar, const FQuantizationOption& Opts, const FBoneRange* Range)   
{
    switch (Opts.Method)
    {
    case ETransformQuantizationMethod::Standard_High:
        {
            FQuantizedTransform_High Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::Standard_Compact:
        {
            FQuantizedTransform_Compact Q;
            Ar << Q;
            return Q.ToTransform();
        }
    case ETransformQuantizationMethod::Standard_Low:
        {
            FQuantizedTransform_Lowest Q;
            Ar << Q;
            return Range ? Q.ToTransform(*Range) : Q.ToTransform();
        }
    default:
        {
            FTransform T;
            Ar << T;
            return T;
        }
    }
}

void SerializeSaveData(FArchive& RawAr, FRecordSaveData& SaveData, FQuantizationOption& QuantOpts)
{
    ComputeBoneRanges(SaveData);

    // 2) Actor 배열 길이
    int32 NumActors = SaveData.RecordActorDataArray.Num();
    RawAr << NumActors;

    for (FRecordActorSaveData& ActorData : SaveData.RecordActorDataArray)
    {
        // Actor 식별자
        RawAr << ActorData.ComponentName;
        RawAr << ActorData.ComponentIntervals;
        RawAr << ActorData.BoneRanges;

        // Frame 개수
        int32 NumFrames = ActorData.RecordedFrames.Num();
        RawAr << NumFrames;

        for (FRecordFrame& Frame : ActorData.RecordedFrames)
        {
            // 타임스탬프, 추가/제거 이벤트
            RawAr << Frame.TimeStamp;
            RawAr << Frame.AddedComponents;
            RawAr << Frame.RemovedComponentNames;
            RawAr << Frame.FrameIndex;

            // ComponentTransforms
            int32 NumComps = Frame.ComponentTransforms.Num();
            RawAr << NumComps;
            for (auto& Pair : Frame.ComponentTransforms)
            {
                RawAr << Pair.Key;
                SerializeQuantizedTransform(RawAr, Pair.Value, QuantOpts, nullptr);
            }

            // SkeletalMeshBoneTransforms
            int32 NumBoneMaps = Frame.SkeletalMeshBoneTransforms.Num();
            RawAr << NumBoneMaps;
            for (auto& BonePair : Frame.SkeletalMeshBoneTransforms)
            {
                RawAr << BonePair.Key;

                const FBoneComponentSpace& Space = BonePair.Value;
                int32 BoneCount = Space.BoneTransforms.Num();
                RawAr << BoneCount;

                const FBoneRange* Range = ActorData.BoneRanges.Find(BonePair.Key);
                for (const FTransform& BoneT : Space.BoneTransforms)
                {
                    SerializeQuantizedTransform(RawAr, BoneT, QuantOpts, Range);
                }
            }
        }
    }
    
}

void DeserializeSaveData(FArchive& DataAr, FRecordSaveData& OutData, const FQuantizationOption& QuantOpts)
{
    // 2) Actor 배열 길이
    int32 NumActors = 0;
    DataAr << NumActors;
    OutData.RecordActorDataArray.Empty(NumActors);

    for (int32 i = 0; i < NumActors; ++i)
    {
        FRecordActorSaveData ActorData;
        DataAr << ActorData.ComponentName;
        DataAr << ActorData.ComponentIntervals;
        DataAr << ActorData.BoneRanges;

        int32 NumFrames = 0;
        DataAr << NumFrames;
        ActorData.RecordedFrames.Empty(NumFrames);

        for (int32 f = 0; f < NumFrames; ++f)
        {
            FRecordFrame Frame;
            DataAr << Frame.TimeStamp;
            DataAr << Frame.AddedComponents;
            DataAr << Frame.RemovedComponentNames;
            DataAr << Frame.FrameIndex; 

            // ComponentTransforms
            int32 NumComps = 0;
            DataAr << NumComps;
            for (int32 c = 0; c < NumComps; ++c)
            {
                FString Key;
                DataAr << Key;
                FTransform T = DeserializeQuantizedTransform(DataAr, QuantOpts, nullptr);
                Frame.ComponentTransforms.Add(Key, T);
            }

            // SkeletalMeshBoneTransforms
            int32 NumBoneMaps = 0;
            DataAr << NumBoneMaps;
            for (int32 bm = 0; bm < NumBoneMaps; ++bm)
            {
                FString Key;
                DataAr << Key;
                int32 BoneCount = 0;
                DataAr << BoneCount;
                FBoneComponentSpace Space;
                Space.BoneTransforms.Empty(BoneCount);
                const FBoneRange* Range = ActorData.BoneRanges.Find(Key);
                for (int32 b = 0; b < BoneCount; ++b)
                {
                    FTransform BoneT = DeserializeQuantizedTransform(DataAr, QuantOpts, Range);
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
