"""Minimal Gemini image-generation client. Key from remaster/.env, never logged."""
import base64, io, json, os, sys, urllib.error, urllib.request
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL = os.environ.get('GEMINI_IMAGE_MODEL', 'gemini-3-pro-image')


def api_key():
    for line in open(os.path.join(ROOT, 'remaster', '.env')):
        if line.startswith('GOOGLE_GENERATIVE_AI_API_KEY='):
            return line.split('=', 1)[1].strip().strip('"\'')
    sys.exit('GOOGLE_GENERATIVE_AI_API_KEY not found in remaster/.env')


def png_b64(im):
    buf = io.BytesIO()
    im.convert('RGB').save(buf, 'PNG')
    return base64.b64encode(buf.getvalue()).decode()


def generate(prompt, images, aspect='16:9', size='2K', label='image'):
    """Send prompt + reference images, return the first generated image (RGB)."""
    parts = [{'text': prompt}] + [{'inline_data': {'mime_type': 'image/png', 'data': png_b64(im)}} for im in images]
    body = {'contents': [{'parts': parts}],
            'generationConfig': {'responseModalities': ['IMAGE'],
                                 'imageConfig': {'aspectRatio': aspect, 'imageSize': size}}}
    req = urllib.request.Request(
        f'https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent',
        data=json.dumps(body).encode(),
        headers={'Content-Type': 'application/json', 'x-goog-api-key': api_key()})
    try:
        resp = json.load(urllib.request.urlopen(req, timeout=300))
    except urllib.error.HTTPError as e:
        sys.exit(f'{label}: HTTP {e.code} {e.read()[:300]!r}')
    parts = resp.get('candidates', [{}])[0].get('content', {}).get('parts', [])
    data = next((p['inlineData']['data'] for p in parts if 'inlineData' in p), None)
    if not data:
        sys.exit(f'{label}: no image in response: {json.dumps(resp)[:300]}')
    return Image.open(io.BytesIO(base64.b64decode(data))).convert('RGB')
