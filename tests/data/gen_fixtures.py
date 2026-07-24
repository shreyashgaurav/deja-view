# We could have used a downloaded image. But to make the project and the tests reproduciblae we use this image generation
# Deterministic test fixtures for decoder tests. Run from tests/data/.
from PIL import Image

# 16x16 horizontal gradient, dark left -> bright right
img = Image.new('L', (16, 16))
for y in range(16):
    for x in range(16):
        img.putpixel((x, y), min(255, x * 17))
rgb = img.convert('RGB') #JPEG doesn't naturally store grayscale in every workflow. So covert to RGB that still looks greyscale: R = G = B = 120
rgb.save('tiny.jpg', 'JPEG', quality=92)
rgb.save('tiny.png', 'PNG')

# 800x600 gradient: exercises the JPEG scaled-decode path
big = Image.new('L', (800, 600))
for x in range(800):
    c = int(x * 255 / 799)
    for y in range(600):
        big.putpixel((x, y), c)
big.convert('RGB').save('big.jpg', 'JPEG', quality=85)
print('fixtures written: tiny.jpg tiny.png big.jpg')
