#include "LogVerbosity.h"

#include <cstring>


const char* LogVerbosityToString( ELogVerbosity Verbosity )
{
	switch ( Verbosity )
	{
	case ELogVerbosity::NoLogging:   return "NoLogging";
	case ELogVerbosity::Fatal:       return "Fatal";
	case ELogVerbosity::Error:       return "Error";
	case ELogVerbosity::Warning:     return "Warning";
	case ELogVerbosity::Display:     return "Display";
	case ELogVerbosity::Log:         return "Log";
	case ELogVerbosity::Verbose:     return "Verbose";
	case ELogVerbosity::VeryVerbose: return "VeryVerbose";
	}
	return "Unknown";
}

bool LogVerbosityFromString( const char* Text, ELogVerbosity& OutVerbosity )
{
	if ( Text == nullptr ) return false;

	static constexpr ELogVerbosity All[] = {
		ELogVerbosity::NoLogging, ELogVerbosity::Fatal, ELogVerbosity::Error, ELogVerbosity::Warning,
		ELogVerbosity::Display, ELogVerbosity::Log, ELogVerbosity::Verbose, ELogVerbosity::VeryVerbose,
	};
	for ( const ELogVerbosity Candidate : All )
	{
		if ( _stricmp( Text, LogVerbosityToString( Candidate ) ) == 0 )
		{
			OutVerbosity = Candidate;
			return true;
		}
	}
	return false;
}
