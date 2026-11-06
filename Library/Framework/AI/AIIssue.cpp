// SPDX-License-Identifier: MIT
#include "AIIssue.h"

#include "Library/Core/Log/Log.h"
#include "Library/Framework/AI/AILog.h"

DEFINE_LOG_CATEGORY( LogAI )

bool NAI::HasErrors( const TArray<FAIIssue>& Issues )
{
	for ( const FAIIssue& Issue : Issues )
	{
		if ( Issue.Severity == EAIIssueSeverity::Error )
		{
			return true;
		}
	}
	return false;
}

void NAI::LogIssues( const FString& AssetPath, const TArray<FAIIssue>& Issues )
{
	for ( const FAIIssue& Issue : Issues )
	{
		switch ( Issue.Severity )
		{
		case EAIIssueSeverity::Info:
			LOG( LogAI, Verbose, "%s: %s: %s", *AssetPath, *Issue.Where, *Issue.Message );
			break;
		case EAIIssueSeverity::Warning:
			LOG( LogAI, Warning, "%s: %s: %s", *AssetPath, *Issue.Where, *Issue.Message );
			break;
		case EAIIssueSeverity::Error:
			LOG( LogAI, Error, "%s: %s: %s", *AssetPath, *Issue.Where, *Issue.Message );
			break;
		}
	}
}

void NAI::AddIssue( TArray<FAIIssue>* Issues, EAIIssueSeverity Severity, const FString& Where, const FString& Message )
{
	if ( Issues == nullptr )
	{
		return;
	}
	FAIIssue Issue;
	Issue.Severity = Severity;
	Issue.Where = Where;
	Issue.Message = Message;
	Issues->Add( Issue );
}
