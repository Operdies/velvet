--- move |rect| by x, y
--- @param rect velvet.api.rect
--- @param x integer the x delta
--- @param y integer the y delta
local function translate(rect, x, y) 
  return { width = rect.width, height = rect.height, left = rect.left + x, top = rect.top + y }
end

--- enlarge |rect| by dx, dy
--- @param rect velvet.api.rect
--- @param dw integer the amount to increase width by
--- @param dh integer the amount to increase height by
local function enlarge(rect, dw, dh)
  return { width = rect.width + dw, height = rect.height + dh, left = rect.left, top = rect.top }
end

--- shrink or grow symmetrically, preserving center (negative n expands)
--- @param rect velvet.api.rect
--- @param n integer inset amount
local function inset(rect, n)
  return { left = rect.left + n, top = rect.top + n, width = rect.width - n*2, height = rect.height - n*2 }
end

return {
  translate = translate,
  grow = enlarge,
  inset = inset,
}
