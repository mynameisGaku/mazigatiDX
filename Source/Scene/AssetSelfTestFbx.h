#pragma once
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

//=============================================================================
// 確認用の FBX（ASCII, 7.4）/ OBJ の生成
//   形・スキン・アニメーションが分かっているモデルを作って、取り込み（FBX -> .x -> DxLib -> .mv1）が
//   座標・向き・骨の動きを正しく運んでいるかを、数値で確かめる。
//
//   モデル「Bar」: 四角柱（断面は x ∈ [-0.5, 0.5]、z ∈ [-0.5, 1.5]。z が非対称なので、座標系の反転が分かる）。
//     輪が 3 段（y = 0, 1, 2）。下の輪は Bone1、上の輪は Bone2 に 100%、真ん中の輪は半々。
//     Bone1 は原点、Bone2 は Bone1 の子で (0, 1, 0)。
//   アニメーション「Wave」（1 秒）: Bone2 が Z 軸まわりに 0 -> 90 度。
//   最後（90 度）のとき、座標は (x, y, z) -> 上の輪は (-1, 1 + x, z)、真ん中の輪は (x / 2, 1 + x / 2, z)、下の輪はそのまま。
//   左手系（DxLib）にすると、z が反転する（-1.5 .. 0.5）。
//=============================================================================
namespace NAssetTest
{
	constexpr long long KTimePerSecond = 46186158000LL;

	struct FBarMesh
	{
		std::vector<std::array<double, 3>> Vertices;    // 12 個（輪 3 段 x 4）
		std::vector<std::array<int, 4>> Quads;          // 外向きの向き（右手系で反時計回り）にそろえた 10 枚（側面 8 + 上下）
		std::vector<std::array<double, 3>> Normals;     // 面ごと（4 頂点に同じ）
	};

	inline FBarMesh MakeBarMesh()
	{
		FBarMesh Mesh;
		const double Corners[4][2] = { { -0.5, -0.5 }, { 0.5, -0.5 }, { 0.5, 1.5 }, { -0.5, 1.5 } };
		for ( int Ring = 0; Ring < 3; ++Ring )
		{
			for ( int Corner = 0; Corner < 4; ++Corner ) Mesh.Vertices.push_back( { Corners[Corner][0], static_cast<double>( Ring ), Corners[Corner][1] } );
		}
		std::vector<std::array<int, 4>> Quads;
		for ( int Ring = 0; Ring < 2; ++Ring )
		{
			for ( int Corner = 0; Corner < 4; ++Corner ) Quads.push_back( { Ring * 4 + Corner, Ring * 4 + ( Corner + 1 ) % 4, ( Ring + 1 ) * 4 + ( Corner + 1 ) % 4, ( Ring + 1 ) * 4 + Corner } );
		}
		Quads.push_back( { 0, 1, 2, 3 } );
		Quads.push_back( { 8, 9, 10, 11 } );

		const double Center[3] = { 0.0, 1.0, 0.5 };
		for ( std::array<int, 4> Quad : Quads )
		{
			const auto& A = Mesh.Vertices[Quad[0]];
			const auto& B = Mesh.Vertices[Quad[1]];
			const auto& C = Mesh.Vertices[Quad[2]];
			double Edge1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
			double Edge2[3] = { C[0] - B[0], C[1] - B[1], C[2] - B[2] };
			double Normal[3] = { Edge1[1] * Edge2[2] - Edge1[2] * Edge2[1], Edge1[2] * Edge2[0] - Edge1[0] * Edge2[2], Edge1[0] * Edge2[1] - Edge1[1] * Edge2[0] };
			double Centroid[3] = { 0, 0, 0 };
			for ( int Index : Quad )
				for ( int Axis = 0; Axis < 3; ++Axis ) Centroid[Axis] += Mesh.Vertices[Index][Axis] / 4.0;
			double Dot = 0.0;
			for ( int Axis = 0; Axis < 3; ++Axis ) Dot += Normal[Axis] * ( Centroid[Axis] - Center[Axis] );
			if ( Dot < 0.0 )
			{
				std::swap( Quad[0], Quad[3] );
				std::swap( Quad[1], Quad[2] );
				for ( double& Value : Normal ) Value = -Value;
			}
			const double Length = std::sqrt( Normal[0] * Normal[0] + Normal[1] * Normal[1] + Normal[2] * Normal[2] );
			Mesh.Quads.push_back( Quad );
			Mesh.Normals.push_back( { Normal[0] / Length, Normal[1] / Length, Normal[2] / Length } );
		}
		return Mesh;
	}

	struct FFbxSpec
	{
		bool bMesh = true;
		bool bSkeleton = true;           // Bone1 / Bone2（メッシュがなくても、アニメーションの対象として出す）
		bool bAnimation = true;
		bool bZUp = false;               // 上の軸が Z の FBX（座標を回して出す。スキン・アニメーションなしのときだけ）
		const char* TextureFile = "sheet.bmp";   // nullptr で画像なし
	};

	namespace Detail
	{
		inline void AppendNumbers( std::string& Out, const std::vector<double>& Values )
		{
			char Buffer[40];
			for ( size_t i = 0; i < Values.size(); ++i )
			{
				std::snprintf( Buffer, sizeof( Buffer ), "%.9g", Values[i] );
				if ( i > 0 ) Out += ",";
				Out += Buffer;
			}
		}
		inline void AppendInts( std::string& Out, const std::vector<int>& Values )
		{
			for ( size_t i = 0; i < Values.size(); ++i )
			{
				if ( i > 0 ) Out += ",";
				Out += std::to_string( Values[i] );
			}
		}
		inline void Identity( std::vector<double>& M ) { M = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; }
	}

	// ASCII FBX（バージョン 7400）。Y が上の右手系が標準
	inline std::string MakeBarFbx( const FFbxSpec& Spec )
	{
		using namespace Detail;
		const FBarMesh Bar = MakeBarMesh();
		std::string F;
		F += "; FBX 7.4.0 project file\n";
		F += "FBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n\tCreator: \"CoG self test\"\n}\n";
		F += "GlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n";
		if ( Spec.bZUp )
		{
			F += "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n";
			F += "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",-1\n";
		}
		else
		{
			F += "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\",1\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n";
			F += "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n";
		}
		F += "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n";
		F += "\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n\t}\n}\n";

		F += "Objects:  {\n";
		if ( Spec.bMesh )
		{
			std::vector<double> Positions;
			for ( const auto& V : Bar.Vertices )
			{
				if ( Spec.bZUp ) Positions.insert( Positions.end(), { V[0], -V[2], V[1] } );   // Y 上 -> Z 上（X 軸まわりに 90 度）
				else Positions.insert( Positions.end(), { V[0], V[1], V[2] } );
			}
			std::vector<int> Indices;
			std::vector<double> Normals, UVs;
			std::vector<int> UVIndices;
			for ( size_t Quad = 0; Quad < Bar.Quads.size(); ++Quad )
			{
				for ( int Corner = 0; Corner < 4; ++Corner )
				{
					const int Index = Bar.Quads[Quad][Corner];
					Indices.push_back( Corner == 3 ? -( Index + 1 ) : Index );
					const auto& N = Bar.Normals[Quad];
					if ( Spec.bZUp ) Normals.insert( Normals.end(), { N[0], -N[2], N[1] } );
					else Normals.insert( Normals.end(), { N[0], N[1], N[2] } );
					UVIndices.push_back( Corner );
				}
			}
			UVs = { 0.0, 0.0, 1.0, 0.0, 1.0, 0.25, 0.0, 0.25 };   // 非対称（V の上下の反転が分かる）

			F += "\tGeometry: 100, \"Geometry::Bar\", \"Mesh\" {\n\t\tVertices: *" + std::to_string( Positions.size() ) + " {\n\t\t\ta: ";
			AppendNumbers( F, Positions );
			F += "\n\t\t}\n\t\tPolygonVertexIndex: *" + std::to_string( Indices.size() ) + " {\n\t\t\ta: ";
			AppendInts( F, Indices );
			F += "\n\t\t}\n\t\tGeometryVersion: 124\n";
			F += "\t\tLayerElementNormal: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"Direct\"\n\t\t\tNormals: *" +
				 std::to_string( Normals.size() ) + " {\n\t\t\t\ta: ";
			AppendNumbers( F, Normals );
			F += "\n\t\t\t}\n\t\t}\n";
			F += "\t\tLayerElementUV: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"UVMap\"\n\t\t\tMappingInformationType: \"ByPolygonVertex\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tUV: *8 {\n\t\t\t\ta: ";
			AppendNumbers( F, UVs );
			F += "\n\t\t\t}\n\t\t\tUVIndex: *" + std::to_string( UVIndices.size() ) + " {\n\t\t\t\ta: ";
			AppendInts( F, UVIndices );
			F += "\n\t\t\t}\n\t\t}\n";
			F += "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"AllSame\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n";
			F += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementNormal\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
			F += "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementUV\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n";
			F += "\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n\t}\n";
			F += "\tModel: 200, \"Model::Bar\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n\t\t}\n\t\tShading: T\n\t\tCulling: \"CullingOff\"\n\t}\n";
			F += "\tMaterial: 400, \"Material::BarMaterial\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"phong\"\n\t\tMultiLayer: 0\n\t\tProperties70:  {\n";
			F += "\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0.25,0.5,0.75\n\t\t}\n\t}\n";
			if ( Spec.TextureFile != nullptr )
			{
				const std::string Name = Spec.TextureFile;
				F += "\tTexture: 500, \"Texture::BarTexture\", \"\" {\n\t\tType: \"TextureVideoClip\"\n\t\tVersion: 202\n\t\tTextureName: \"Texture::BarTexture\"\n\t\tMedia: \"Video::BarTexture\"\n";
				F += "\t\tFileName: \"" + Name + "\"\n\t\tRelativeFilename: \"" + Name + "\"\n\t}\n";
				F += "\tVideo: 600, \"Video::BarTexture\", \"Clip\" {\n\t\tType: \"Clip\"\n\t\tProperties70:  {\n\t\t\tP: \"Path\", \"KString\", \"XRefUrl\", \"\", \"" + Name + "\"\n\t\t}\n";
				F += "\t\tUseMipMap: 0\n\t\tFilename: \"" + Name + "\"\n\t\tRelativeFilename: \"" + Name + "\"\n\t}\n";
			}
		}
		if ( Spec.bSkeleton )
		{
			F += "\tNodeAttribute: 300, \"NodeAttribute::Bone1\", \"LimbNode\" {\n\t\tTypeFlags: \"Skeleton\"\n\t}\n";
			F += "\tNodeAttribute: 301, \"NodeAttribute::Bone2\", \"LimbNode\" {\n\t\tTypeFlags: \"Skeleton\"\n\t}\n";
			F += "\tModel: 201, \"Model::Bone1\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n\t\t}\n\t}\n";
			F += "\tModel: 202, \"Model::Bone2\", \"LimbNode\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t\tP: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,1,0\n";
			F += "\t\t\tP: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,0\n\t\t}\n\t}\n";
		}
		if ( Spec.bMesh && Spec.bSkeleton )
		{
			// スキン: 下の輪（頂点 0..3）は Bone1、上の輪（8..11）は Bone2、真ん中（4..7）は半々
			F += "\tDeformer: 700, \"Deformer::BarSkin\", \"Skin\" {\n\t\tVersion: 101\n\t\tLink_DeformAcuracy: 50\n\t\tSkinningType: \"Linear\"\n\t}\n";
			// Transform は、メッシュのノードからその骨への変換（= 骨のワールド行列の逆行列 x メッシュのワールド行列。ufbx もそう読む）、
			// TransformLink は、骨のバインド時のワールド行列
			std::vector<double> Bind1, Bind2, MeshToBone1, MeshToBone2;
			Identity( Bind1 );
			Identity( Bind2 );
			Identity( MeshToBone1 );
			Identity( MeshToBone2 );
			Bind2[13] = 1.0;           // Bone2 のバインド時のワールド位置 (0, 1, 0)
			MeshToBone2[13] = -1.0;    // メッシュ（原点）から見た Bone2 は (0, -1, 0) 動かす
			F += "\tDeformer: 701, \"SubDeformer::Bone1\", \"Cluster\" {\n\t\tVersion: 100\n\t\tUserData: \"\", \"\"\n\t\tIndexes: *8 {\n\t\t\ta: 0,1,2,3,4,5,6,7\n\t\t}\n";
			F += "\t\tWeights: *8 {\n\t\t\ta: 1,1,1,1,0.5,0.5,0.5,0.5\n\t\t}\n\t\tTransform: *16 {\n\t\t\ta: ";
			AppendNumbers( F, MeshToBone1 );
			F += "\n\t\t}\n\t\tTransformLink: *16 {\n\t\t\ta: ";
			AppendNumbers( F, Bind1 );
			F += "\n\t\t}\n\t}\n";
			F += "\tDeformer: 702, \"SubDeformer::Bone2\", \"Cluster\" {\n\t\tVersion: 100\n\t\tUserData: \"\", \"\"\n\t\tIndexes: *8 {\n\t\t\ta: 4,5,6,7,8,9,10,11\n\t\t}\n";
			F += "\t\tWeights: *8 {\n\t\t\ta: 0.5,0.5,0.5,0.5,1,1,1,1\n\t\t}\n\t\tTransform: *16 {\n\t\t\ta: ";
			AppendNumbers( F, MeshToBone2 );
			F += "\n\t\t}\n\t\tTransformLink: *16 {\n\t\t\ta: ";
			AppendNumbers( F, Bind2 );
			F += "\n\t\t}\n\t}\n";
		}
		if ( Spec.bAnimation && Spec.bSkeleton )
		{
			F += "\tAnimationStack: 800, \"AnimStack::Wave\", \"\" {\n\t\tProperties70:  {\n\t\t\tP: \"LocalStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"LocalStop\", \"KTime\", \"Time\", \"\"," +
				 std::to_string( KTimePerSecond ) + "\n\t\t\tP: \"ReferenceStart\", \"KTime\", \"Time\", \"\",0\n\t\t\tP: \"ReferenceStop\", \"KTime\", \"Time\", \"\"," + std::to_string( KTimePerSecond ) + "\n\t\t}\n\t}\n";
			F += "\tAnimationLayer: 801, \"AnimLayer::BaseLayer\", \"\" {\n\t}\n";
			F += "\tAnimationCurveNode: 802, \"AnimCurveNode::R\", \"\" {\n\t\tProperties70:  {\n\t\t\tP: \"d|X\", \"Number\", \"\", \"A\",0\n\t\t\tP: \"d|Y\", \"Number\", \"\", \"A\",0\n\t\t\tP: \"d|Z\", \"Number\", \"\", \"A\",0\n\t\t}\n\t}\n";
			F += "\tAnimationCurve: 803, \"AnimCurve::\", \"\" {\n\t\tDefault: 0\n\t\tKeyVer: 4009\n\t\tKeyTime: *2 {\n\t\t\ta: 0," + std::to_string( KTimePerSecond ) + "\n\t\t}\n";
			F += "\t\tKeyValueFloat: *2 {\n\t\t\ta: 0,90\n\t\t}\n\t\tKeyAttrFlags: *1 {\n\t\t\ta: 24836\n\t\t}\n\t\tKeyAttrDataFloat: *4 {\n\t\t\ta: 0,0,218434821,0\n\t\t}\n\t\tKeyAttrRefCount: *1 {\n\t\t\ta: 2\n\t\t}\n\t}\n";
		}
		F += "}\n";

		F += "Connections:  {\n";
		if ( Spec.bMesh )
		{
			F += "\tC: \"OO\",200,0\n\tC: \"OO\",100,200\n\tC: \"OO\",400,200\n";
			if ( Spec.TextureFile != nullptr ) F += "\tC: \"OP\",500,400,\"DiffuseColor\"\n\tC: \"OO\",600,500\n";
		}
		if ( Spec.bSkeleton ) F += "\tC: \"OO\",201,0\n\tC: \"OO\",202,201\n\tC: \"OO\",300,201\n\tC: \"OO\",301,202\n";
		if ( Spec.bMesh && Spec.bSkeleton ) F += "\tC: \"OO\",700,100\n\tC: \"OO\",701,700\n\tC: \"OO\",702,700\n\tC: \"OO\",201,701\n\tC: \"OO\",202,702\n";
		if ( Spec.bAnimation && Spec.bSkeleton ) F += "\tC: \"OO\",801,800\n\tC: \"OO\",802,801\n\tC: \"OP\",802,202,\"Lcl Rotation\"\n\tC: \"OP\",803,802,\"d|Z\"\n";
		F += "}\n";
		return F;
	}

	// OBJ の直方体（x ∈ [-1, 1]、y ∈ [0, 2]、z ∈ [-1, 2]）と、マテリアルファイル
	inline std::string MakeBoxObj( const char* MaterialFile )
	{
		const double V[8][3] = { { -1, 0, -1 }, { 1, 0, -1 }, { 1, 2, -1 }, { -1, 2, -1 }, { -1, 0, 2 }, { 1, 0, 2 }, { 1, 2, 2 }, { -1, 2, 2 } };
		// 外向き（反時計回り）の面
		const int Faces[6][4] = { { 1, 4, 3, 2 }, { 5, 6, 7, 8 }, { 1, 2, 6, 5 }, { 4, 8, 7, 3 }, { 1, 5, 8, 4 }, { 2, 3, 7, 6 } };
		std::string S = "# box\n";
		if ( MaterialFile != nullptr ) S += std::string( "mtllib " ) + MaterialFile + "\n";
		S += "o Box\n";
		char Buffer[64];
		for ( const auto& P : V )
		{
			std::snprintf( Buffer, sizeof( Buffer ), "v %g %g %g\n", P[0], P[1], P[2] );
			S += Buffer;
		}
		S += "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n";
		if ( MaterialFile != nullptr ) S += "usemtl BoxMaterial\n";
		for ( const auto& Face : Faces )
		{
			S += "f";
			for ( int Corner = 0; Corner < 4; ++Corner ) S += " " + std::to_string( Face[Corner] ) + "/" + std::to_string( Corner + 1 );
			S += "\n";
		}
		return S;
	}
}
