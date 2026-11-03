#include "Scene/CLSceneWindow.h"
bool FCLSceneWindow::Validate(FString& Error) const
{
    if(!FCLBodyFrame{AnchorMeters,BodyFromLocal}.Validate(Error))return false;
    if(HalfExtentMeters.ContainsNaN()||!FMath::IsFinite(HalfExtentMeters.SizeSquared())||HalfExtentMeters.GetMin()<1||HalfExtentMeters.GetMax()>2048)
    {Error=TEXT("Native coverage requires finite half extents between 1 and 2048 metres");return false;}
    Error.Reset();return true;
}
