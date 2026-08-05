uniform sampler2D u_Tex0;
uniform vec2 u_Resolution;
varying vec2 v_TexCoord;

float luma(vec3 color)
{
  return dot(color, vec3(0.299, 0.587, 0.114));
}

void main()
{
  vec2 inverseVP = vec2(1.0 / u_Resolution.x, 1.0 / u_Resolution.y);

  vec3 rgbNW = texture2D(u_Tex0, v_TexCoord + vec2(-1.0, -1.0) * inverseVP).xyz;
  vec3 rgbNE = texture2D(u_Tex0, v_TexCoord + vec2( 1.0, -1.0) * inverseVP).xyz;
  vec3 rgbSW = texture2D(u_Tex0, v_TexCoord + vec2(-1.0,  1.0) * inverseVP).xyz;
  vec3 rgbSE = texture2D(u_Tex0, v_TexCoord + vec2( 1.0,  1.0) * inverseVP).xyz;
  vec4 centerSample = texture2D(u_Tex0, v_TexCoord);
  vec3 rgbM = centerSample.xyz;

  float lumaNW = luma(rgbNW);
  float lumaNE = luma(rgbNE);
  float lumaSW = luma(rgbSW);
  float lumaSE = luma(rgbSE);
  float lumaM  = luma(rgbM);

  float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
  float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
  float edgeRange = lumaMax - lumaMin;

  vec3 aaBase = rgbM;

  if (edgeRange >= 0.045) {
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
      (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.125),
      0.001
    );

    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-5.0), vec2(5.0)) * inverseVP;

    vec3 rgbA = 0.5 * (
      texture2D(u_Tex0, v_TexCoord + dir * (1.0 / 3.0 - 0.5)).xyz +
      texture2D(u_Tex0, v_TexCoord + dir * (2.0 / 3.0 - 0.5)).xyz
    );

    vec3 rgbB = rgbA * 0.5 + 0.25 * (
      texture2D(u_Tex0, v_TexCoord + dir * -0.5).xyz +
      texture2D(u_Tex0, v_TexCoord + dir * 0.5).xyz
    );

    float lumaB = luma(rgbB);
    vec3 aaColor = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;
    float aaBlend = clamp((edgeRange - 0.045) / 0.16, 0.0, 1.0) * 0.62;
    aaBase = mix(rgbM, aaColor, aaBlend);
  }

  vec3 blurCross = (
    texture2D(u_Tex0, v_TexCoord + vec2( inverseVP.x, 0.0)).xyz +
    texture2D(u_Tex0, v_TexCoord + vec2(-inverseVP.x, 0.0)).xyz +
    texture2D(u_Tex0, v_TexCoord + vec2(0.0,  inverseVP.y)).xyz +
    texture2D(u_Tex0, v_TexCoord + vec2(0.0, -inverseVP.y)).xyz
  ) * 0.25;

  vec3 sharpened = aaBase + (aaBase - blurCross) * 0.18;
  float sharpenLuma = luma(sharpened);
  vec3 saturated = mix(vec3(sharpenLuma), sharpened, 1.035);
  vec3 contrasted = (saturated - 0.5) * 1.075 + 0.5;
  vec3 finalColor = pow(clamp(contrasted, 0.0, 1.0), vec3(0.985));

  gl_FragColor = vec4(finalColor, centerSample.a);
}
