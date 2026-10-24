uniform vec4 gRoundedRectSdfData[5];

noperspective in vec2 texCoord;

out vec4 FragClr;

float RoundedRectSdf(vec2 Point, vec2 HalfSize, float Radius)
{
	vec2 DistanceToInner = abs(Point) - HalfSize + Radius;
	return length(max(DistanceToInner, vec2(0.0))) + min(max(DistanceToInner.x, DistanceToInner.y), 0.0) - Radius;
}

float CornerRadius(vec2 Point, vec4 CornerRadii)
{
	if(Point.y < 0.0)
		return Point.x < 0.0 ? CornerRadii.x : CornerRadii.y;
	return Point.x < 0.0 ? CornerRadii.w : CornerRadii.z;
}

float SdfFeather(float DistanceValue, float PixelSize)
{
	return max(PixelSize, length(vec2(dFdx(DistanceValue), dFdy(DistanceValue))));
}

float Coverage(float DistanceValue, float PixelSize)
{
	float Feather = SdfFeather(DistanceValue, PixelSize);
	return 1.0 - smoothstep(-Feather * 0.5, Feather * 0.5, DistanceValue);
}

void main()
{
	vec4 Rect = gRoundedRectSdfData[0];
	vec4 FillColor = gRoundedRectSdfData[1];
	vec4 BorderColor = gRoundedRectSdfData[2];
	vec4 CornerRadii = gRoundedRectSdfData[3];
	vec4 Params = gRoundedRectSdfData[4];
	vec2 HalfSize = Rect.zw * 0.5;
	vec2 Point = (texCoord - vec2(0.5)) * (Rect.zw + vec2(Params.z * 2.0));
	float Radius = clamp(CornerRadius(Point, CornerRadii), 0.0, min(HalfSize.x, HalfSize.y));
	float OuterDistance = RoundedRectSdf(Point, HalfSize, Radius);
	float OuterCoverage = Coverage(OuterDistance, Params.y);
	float BorderWidth = clamp(Params.x, 0.0, min(HalfSize.x, HalfSize.y));
	vec2 InnerHalfSize = HalfSize - vec2(BorderWidth);
	vec4 InnerCornerRadii = max(CornerRadii - vec4(BorderWidth), vec4(0.0));
	float InnerRadius = clamp(CornerRadius(Point, InnerCornerRadii), 0.0, min(InnerHalfSize.x, InnerHalfSize.y));
	float InnerDistance = RoundedRectSdf(Point, InnerHalfSize, InnerRadius);
	float InnerCoverage = BorderWidth > 0.0 && min(InnerHalfSize.x, InnerHalfSize.y) > 0.0 ? Coverage(InnerDistance, Params.y) : 0.0;
	if(BorderWidth <= 0.0)
	{
		FragClr = vec4(FillColor.rgb, FillColor.a * OuterCoverage);
		return;
	}
	float BorderCoverage = max(OuterCoverage - InnerCoverage, 0.0);
	float BorderAlpha = BorderColor.a * BorderCoverage;
	float FillAlpha = FillColor.a * InnerCoverage;
	float OutputAlpha = FillAlpha + BorderAlpha;
	vec3 Premultiplied = FillColor.rgb * FillAlpha + BorderColor.rgb * BorderAlpha;
	FragClr = OutputAlpha > 0.0 ? vec4(Premultiplied / OutputAlpha, OutputAlpha) : vec4(0.0);
}
