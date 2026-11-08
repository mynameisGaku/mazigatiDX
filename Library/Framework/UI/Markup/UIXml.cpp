// SPDX-License-Identifier: MIT
#include "UIXml.h"

#include "Library/Core/Types/CString.h"

namespace
{
	constexpr int32 MaxDepth = 100;

	bool IsNameStart( char Ch )
	{
		return ( Ch >= 'A' && Ch <= 'Z' ) || ( Ch >= 'a' && Ch <= 'z' ) || Ch == '_' || Ch == ':' || ( static_cast<unsigned char>( Ch ) >= 0x80 );
	}

	bool IsNameChar( char Ch )
	{
		return IsNameStart( Ch ) || ( Ch >= '0' && Ch <= '9' ) || Ch == '-' || Ch == '.';
	}

	bool IsSpace( char Ch )
	{
		return Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n';
	}

	// コードポイントを UTF-8 にして、文字列へ足す
	void AppendUtf8( FString& Text, uint32 CodePoint )
	{
		if ( CodePoint < 0x80 )
		{
			Text += static_cast<char>( CodePoint );
		}
		else if ( CodePoint < 0x800 )
		{
			Text += static_cast<char>( 0xC0 | ( CodePoint >> 6 ) );
			Text += static_cast<char>( 0x80 | ( CodePoint & 0x3F ) );
		}
		else if ( CodePoint < 0x10000 )
		{
			Text += static_cast<char>( 0xE0 | ( CodePoint >> 12 ) );
			Text += static_cast<char>( 0x80 | ( ( CodePoint >> 6 ) & 0x3F ) );
			Text += static_cast<char>( 0x80 | ( CodePoint & 0x3F ) );
		}
		else
		{
			Text += static_cast<char>( 0xF0 | ( CodePoint >> 18 ) );
			Text += static_cast<char>( 0x80 | ( ( CodePoint >> 12 ) & 0x3F ) );
			Text += static_cast<char>( 0x80 | ( ( CodePoint >> 6 ) & 0x3F ) );
			Text += static_cast<char>( 0x80 | ( CodePoint & 0x3F ) );
		}
	}

	class FParser
	{
	public:
		explicit FParser( const FString& Text )
			: Cursor( *Text )
			, End( *Text + Text.Len() )
		{
		}

		FUIXmlResult Run()
		{
			FUIXmlResult Result;
			SkipMisc();
			if ( !bFailed && ( Cursor >= End || *Cursor != '<' ) )
			{
				Fail( "the document has no root element" );
			}
			TSharedPtr<FUIXmlNode> Root;
			if ( !bFailed )
			{
				Root = ParseElement( 1 );
			}
			if ( !bFailed )
			{
				SkipMisc();
				if ( !bFailed && Cursor < End )
				{
					Fail( "there is more than one root element, or extra content after the root element" );
				}
			}
			if ( bFailed )
			{
				Result.Error = FString::Printf( "line %d: %s", ErrorLine, *Error );
				Result.ErrorLine = ErrorLine;
				return Result;
			}
			Result.bSuccess = true;
			Result.Root = Root;
			return Result;
		}

	private:
		void Fail( const char* Message )
		{
			if ( !bFailed )
			{
				bFailed = true;
				Error = Message;
				ErrorLine = Line;
			}
		}

		bool StartsWith( const char* Prefix ) const
		{
			const size_t Length = NCString::Length( Prefix );
			return static_cast<size_t>( End - Cursor ) >= Length && NCString::CompareN( Cursor, Prefix, Length ) == 0;
		}

		void Advance( int32 Count )
		{
			for ( int32 Index = 0; Index < Count && Cursor < End; ++Index )
			{
				Line += *Cursor == '\n' ? 1 : 0;
				++Cursor;
			}
		}

		void SkipSpaces()
		{
			while ( Cursor < End && IsSpace( *Cursor ) )
			{
				Advance( 1 );
			}
		}

		// 閉じの印 Terminator まで読み飛ばす（無ければ失敗）
		void SkipUntil( const char* Terminator, const char* FailMessage )
		{
			const size_t Length = NCString::Length( Terminator );
			while ( Cursor < End )
			{
				if ( StartsWith( Terminator ) )
				{
					Advance( static_cast<int32>( Length ) );
					return;
				}
				Advance( 1 );
			}
			Fail( FailMessage );
		}

		// 要素の外の、空白・コメント・処理命令（<?xml ... ?>）を読み飛ばす
		void SkipMisc()
		{
			while ( !bFailed )
			{
				SkipSpaces();
				if ( StartsWith( "<?" ) )
				{
					Advance( 2 );
					SkipUntil( "?>", "a processing instruction is not closed ('?>' is missing)" );
				}
				else if ( StartsWith( "<!--" ) )
				{
					Advance( 4 );
					SkipUntil( "-->", "a comment is not closed ('-->' is missing)" );
				}
				else
				{
					return;
				}
			}
		}

		bool ReadName( FString& OutName )
		{
			OutName.Empty();
			if ( Cursor >= End || !IsNameStart( *Cursor ) )
			{
				return false;
			}
			while ( Cursor < End && IsNameChar( *Cursor ) )
			{
				OutName += *Cursor;
				Advance( 1 );
			}
			return true;
		}

		// "&...;" を 1 つ読んで、Out へ足す（Cursor は '&' を指している）
		void ReadEntity( FString& Out )
		{
			Advance( 1 );
			FString Name;
			while ( Cursor < End && *Cursor != ';' && Name.Len() < 12 )
			{
				Name += *Cursor;
				Advance( 1 );
			}
			if ( Cursor >= End || *Cursor != ';' )
			{
				Fail( "an entity is not closed (';' is missing)" );
				return;
			}
			Advance( 1 );
			if ( Name == "lt" )
			{
				Out += '<';
			}
			else if ( Name == "gt" )
			{
				Out += '>';
			}
			else if ( Name == "amp" )
			{
				Out += '&';
			}
			else if ( Name == "quot" )
			{
				Out += '"';
			}
			else if ( Name == "apos" )
			{
				Out += '\'';
			}
			else if ( Name.Len() >= 2 && Name.Left( 1 ) == "#" )
			{
				const bool bHex = Name.Mid( 1, 1 ) == "x" || Name.Mid( 1, 1 ) == "X";
				const char* Digits = *Name + ( bHex ? 2 : 1 );
				uint32 CodePoint = 0;
				bool bValid = *Digits != '\0';
				for ( ; *Digits != '\0' && bValid; ++Digits )
				{
					const char Ch = *Digits;
					uint32 Digit = 0;
					if ( Ch >= '0' && Ch <= '9' )
					{
						Digit = static_cast<uint32>( Ch - '0' );
					}
					else if ( bHex && Ch >= 'a' && Ch <= 'f' )
					{
						Digit = static_cast<uint32>( Ch - 'a' + 10 );
					}
					else if ( bHex && Ch >= 'A' && Ch <= 'F' )
					{
						Digit = static_cast<uint32>( Ch - 'A' + 10 );
					}
					else
					{
						bValid = false;
					}
					CodePoint = CodePoint * ( bHex ? 16u : 10u ) + Digit;
					if ( CodePoint > 0x10FFFF )
					{
						bValid = false;
					}
				}
				if ( !bValid || CodePoint == 0 )
				{
					Fail( "a character reference is not valid" );
					return;
				}
				AppendUtf8( Out, CodePoint );
			}
			else
			{
				Fail( "an unknown entity (only &lt; &gt; &amp; &quot; &apos; and character references are supported)" );
			}
		}

		TSharedPtr<FUIXmlNode> ParseElement( int32 Depth )
		{
			if ( Depth > MaxDepth )
			{
				Fail( "the elements are nested too deeply (more than 100 levels)" );
				return TSharedPtr<FUIXmlNode>();
			}
			TSharedPtr<FUIXmlNode> Node = MakeShared<FUIXmlNode>();
			Node->Line = Line;
			Advance( 1 ); // '<'
			if ( !ReadName( Node->Name ) )
			{
				Fail( "an element name is expected after '<'" );
				return Node;
			}

			// 属性
			while ( !bFailed )
			{
				SkipSpaces();
				if ( Cursor >= End )
				{
					Fail( "the tag is not closed" );
					return Node;
				}
				if ( StartsWith( "/>" ) )
				{
					Advance( 2 );
					return Node; // 自己閉じ
				}
				if ( *Cursor == '>' )
				{
					Advance( 1 );
					break;
				}
				FUIXmlAttribute Attribute;
				if ( !ReadName( Attribute.Name ) )
				{
					Fail( "an attribute name is expected" );
					return Node;
				}
				for ( int32 Index = 0; Index < Node->Attributes.Num(); ++Index )
				{
					if ( Node->Attributes[Index].Name == Attribute.Name )
					{
						Fail( "an attribute appears twice in the same tag" );
						return Node;
					}
				}
				SkipSpaces();
				if ( Cursor >= End || *Cursor != '=' )
				{
					Fail( "'=' is expected after an attribute name" );
					return Node;
				}
				Advance( 1 );
				SkipSpaces();
				if ( Cursor >= End || ( *Cursor != '"' && *Cursor != '\'' ) )
				{
					Fail( "an attribute value must be quoted" );
					return Node;
				}
				const char Quote = *Cursor;
				Advance( 1 );
				while ( Cursor < End && *Cursor != Quote && !bFailed )
				{
					if ( *Cursor == '&' )
					{
						ReadEntity( Attribute.Value );
					}
					else if ( *Cursor == '<' )
					{
						Fail( "'<' is not allowed in an attribute value (write &lt;)" );
					}
					else
					{
						Attribute.Value += *Cursor;
						Advance( 1 );
					}
				}
				if ( bFailed )
				{
					return Node;
				}
				if ( Cursor >= End )
				{
					Fail( "an attribute value is not closed" );
					return Node;
				}
				Advance( 1 ); // 閉じの引用符
				Node->Attributes.Add( Attribute );
			}

			// 中身
			FString Text;
			while ( !bFailed )
			{
				if ( Cursor >= End )
				{
					Fail( "an element is not closed" );
					return Node;
				}
				if ( StartsWith( "</" ) )
				{
					const int32 ClosingLine = Line;
					Advance( 2 );
					FString ClosingName;
					if ( !ReadName( ClosingName ) )
					{
						Fail( "an element name is expected after '</'" );
						return Node;
					}
					SkipSpaces();
					if ( Cursor >= End || *Cursor != '>' )
					{
						Fail( "'>' is expected at the end of a closing tag" );
						return Node;
					}
					Advance( 1 );
					if ( ClosingName != Node->Name )
					{
						Line = ClosingLine;
						Fail( "a closing tag does not match its opening tag" );
						return Node;
					}
					Node->Text = Text.TrimStartAndEnd();
					return Node;
				}
				if ( StartsWith( "<!--" ) )
				{
					Advance( 4 );
					SkipUntil( "-->", "a comment is not closed ('-->' is missing)" );
				}
				else if ( StartsWith( "<![CDATA[" ) )
				{
					Advance( 9 );
					while ( Cursor < End && !StartsWith( "]]>" ) )
					{
						Text += *Cursor;
						Advance( 1 );
					}
					if ( Cursor >= End )
					{
						Fail( "a CDATA section is not closed (']]>' is missing)" );
						return Node;
					}
					Advance( 3 );
				}
				else if ( *Cursor == '<' )
				{
					TSharedPtr<FUIXmlNode> Child = ParseElement( Depth + 1 );
					if ( bFailed )
					{
						return Node;
					}
					Node->Children.Add( Child );
				}
				else if ( *Cursor == '&' )
				{
					ReadEntity( Text );
				}
				else
				{
					Text += *Cursor;
					Advance( 1 );
				}
			}
			return Node;
		}

		const char* Cursor;
		const char* End;
		int32 Line = 1;
		bool bFailed = false;
		FString Error;
		int32 ErrorLine = 0;
	};
}

const FString* FUIXmlNode::FindAttribute( const char* AttributeName ) const
{
	for ( int32 Index = 0; Index < Attributes.Num(); ++Index )
	{
		if ( Attributes[Index].Name == AttributeName )
		{
			return &Attributes[Index].Value;
		}
	}
	return nullptr;
}

FString FUIXmlNode::GetAttribute( const char* AttributeName, const FString& Default ) const
{
	const FString* Value = FindAttribute( AttributeName );
	return Value != nullptr ? *Value : Default;
}

FUIXmlResult NUIXml::Parse( const FString& Text )
{
	FParser Parser( Text );
	return Parser.Run();
}
