uniform sampler2D firstPass;
varying vec2 texcoord;

void main() {
    vec4 texel = texture2D(firstPass, texcoord);
    gl_FragColor = texel / texel.w;
}