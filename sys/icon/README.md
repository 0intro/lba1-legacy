# The icon

`LBA1_-_Twinsun.png` is a screenshot of Little Big Adventure (1994),
Twinsun between its two suns, by Adeline Software International
(developer). Its copyright holder, Didier Chanfray S.a.r.l., approved
its publication under the Creative Commons Attribution-ShareAlike 3.0
Unported licence (CC BY-SA 3.0, `LICENSE`), as Wikimedia Commons keeps
the permission (VRT ticket 2014072310006528). It is the only revision
of https://commons.wikimedia.org/wiki/File:LBA1_-_Twinsun.png, uploaded
on 4 July 2014, SHA-1 27b2c344e80a620a53f8ef4307bb2dd27ceb1788, kept as
it is. The program's icon, `lba.bmp` and `lba.ico`, is cut from it
under the same licence, by ImageMagick 7.1.2 in this directory:

    magick LBA1_-_Twinsun.png -crop 344x344+224+80 +repage \
        \( -size 344x344 xc:black -fill white -draw 'circle 172,172 172,0' \) \
        -alpha off -compose CopyOpacity -composite -resize 256x256 -strip BMP:lba.bmp
    magick lba.bmp -define icon:auto-resize=256,128,64,48,32,24,16 lba.ico
