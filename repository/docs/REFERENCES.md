# Источники и другие реализации

Этот проект использует известный подход к исправлению адреса сервера изображений Roblox. Он не позиционируется как первый или единственный фикс.

## Сообщение о проблеме

- [Roblox Developer Forum: CDN subdomain tr.rbxcdn.com does not work in Russia](https://devforum.roblox.com/t/cdn-subdomain-trrbxcdncom-does-not-work-in-russia-avatarsgame-iconsthumbnails-do-not-load/4757714) — обсуждение пропавших аватарок, иконок и превью, включая варианты изменения DNS и hosts.

## Документация

- [Google DNS-over-HTTPS JSON API](https://developers.google.com/speed/public-dns/docs/doh/json).
- [Cloudflare DNS JSON API](https://developers.cloudflare.com/1.1.1.1/encryption/dns-over-https/make-api-requests/dns-json/).
- [curl --resolve](https://curl.se/docs/manpage.html#--resolve).
- [Windows ReplaceFile](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew).

## Аналогичные проекты

- [Abrikosikkk/Fix-Icons-Roblox](https://github.com/Abrikosikkk/Fix-Icons-Roblox) — исправление через получение IP и изменение hosts.
- [mISka-cmd/RobloxPNGBack](https://github.com/mISka-cmd/RobloxPNGBack) — локальный DNS-over-HTTPS прокси для доменов CDN Roblox.
- [wast3r1/RobloxImageFix](https://github.com/wast3r1/RobloxImageFix) — утилита настройки доступа к CDN с графическим интерфейсом.

Ссылки приведены для контекста, а не как гарантия безопасности, совместимости или качества сторонних программ.
