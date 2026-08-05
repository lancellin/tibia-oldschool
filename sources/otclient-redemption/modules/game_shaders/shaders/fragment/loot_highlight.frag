uniform float u_Time;
uniform sampler2D u_Tex0;
varying vec2 v_TexCoord;

void main()
{
	vec4 col = texture2D(u_Tex0, v_TexCoord);
	if (col.a < 0.01) {
		discard;
	}

	float pulse = (cos(u_Time * 3.19) + 1.0) / 2.0;
	float brightness = 1.15 + pulse * 0.35;
	vec3 illuminatedColor = min(col.rgb * brightness + vec3(0.08 * pulse), vec3(1.0));

	gl_FragColor = vec4(illuminatedColor, col.a);
}
