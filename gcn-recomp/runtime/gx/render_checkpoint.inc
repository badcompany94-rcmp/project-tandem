// Included by the GL backend, whose private resources are owned by this thread.
// GPU-resident snapshots share immutable textures; mutable render targets are
// retained with copy-on-write or copied on the GPU. Only matches() reads pixels
// back to the CPU, for the development correctness proof.
#ifndef GCN_GL_ES
struct TextureImage {
    GLint min_filter=0,mag_filter=0,wrap_s=0,wrap_t=0,max_level=0;
    struct Level {
        GLint width=0,height=0,internal=0;
        bool depth=false;
        std::vector<uint32_t> pixels;
        bool operator==(const Level&) const = default;
    };
    std::vector<Level> levels;
    bool operator==(const TextureImage&) const = default;
};
static void checkpoint_gl_ok() {
    const auto error=glGetError();
    if(error!=GL_NO_ERROR)throw std::runtime_error("GL error during render checkpoint: "+std::to_string(error));
}
static TextureImage read_texture(GLuint tex,unsigned count,bool depth=false) {
    TextureImage image;
    glBindTexture(GL_TEXTURE_2D,tex);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&image.min_filter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&image.mag_filter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&image.wrap_s);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&image.wrap_t);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&image.max_level);
    for(unsigned level=0;level<count;++level) {
        TextureImage::Level pixels;pixels.depth=depth;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_WIDTH,&pixels.width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_HEIGHT,&pixels.height);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,level,GL_TEXTURE_INTERNAL_FORMAT,&pixels.internal);
        if(pixels.width<=0 || pixels.height<=0 || pixels.width>16384 || pixels.height>16384)
            throw std::runtime_error("Invalid checkpoint texture dimensions");
        pixels.pixels.resize(size_t(pixels.width)*pixels.height);
        glGetTexImage(GL_TEXTURE_2D,level,depth?GL_DEPTH_COMPONENT:GL_RGBA,
            depth?(pixels.internal==GL_DEPTH_COMPONENT32F?GL_FLOAT:GL_UNSIGNED_INT):GL_UNSIGNED_BYTE,pixels.pixels.data());
        image.levels.push_back(std::move(pixels));
    }
    return image;
}
static void write_texture(GLuint tex,const TextureImage& image) {
    glBindTexture(GL_TEXTURE_2D,tex);
    for(size_t level=0;level<image.levels.size();++level) {
        const auto& p=image.levels[level];
        glTexImage2D(GL_TEXTURE_2D,GLint(level),p.internal,p.width,p.height,0,
            p.depth?GL_DEPTH_COMPONENT:GL_RGBA,p.depth?(p.internal==GL_DEPTH_COMPONENT32F?GL_FLOAT:GL_UNSIGNED_INT):GL_UNSIGNED_BYTE,p.pixels.data());
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,image.min_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,image.mag_filter);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,image.wrap_s);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,image.wrap_t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,image.max_level);
}
struct RenderImage {
    TextureImage color,depth;
    struct Tex {
        GlTex metadata;
        TextureImage image;
        bool operator==(const Tex& other) const {
            const auto& a=metadata;const auto& b=other.metadata;
            return a.w==b.w && a.h==b.h && a.levels==b.levels && a.efb==b.efb && a.grab==b.grab &&
                a.depth==b.depth && a.last_used==b.last_used && a.hash==b.hash && image==other.image;
        }
    };
    struct Display {
        uint32_t base,stride,height,written_rows;
        std::vector<uint8_t> written;
        TextureImage image;
        bool operator==(const Display&) const = default;
    };
    std::unordered_map<uint32_t,Tex> textures;
    std::vector<Display> displays;
    uint32_t frame,present,taken;
    int scale;
    std::vector<uint32_t> dead;
    std::array<float,4> zplane;
    bool operator==(const RenderImage&) const = default;
};
static void checkpoint_texture_access() {
    collect_texture_garbage();
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    gl_state_invalidate();uniforms_invalidate();
}
static RenderImage read_render_image() {
    if(g_vr_active || g_flat_eye!=0 || (g_frame_hook && !g_frame_hook_batch_only) ||
       (g_flat_overlay_hook && !g_flat_overlay_batch_only) || g_fp_on || getenv("GCN_THEATER_STEREO"))
        throw std::logic_error("Render checkpoints currently require a flat desktop view");
    checkpoint_gl_ok();checkpoint_texture_access();
    RenderImage image;
    image.scale=g_scale;image.frame=g_render_frame;image.present=g_present_count;image.taken=frames_taken();image.dead=g_dead_pending;
    std::copy(std::begin(g_zplane),std::end(g_zplane),image.zplane.begin());
    image.color=read_texture(g_efb_color,1);image.depth=read_texture(g_efb_depth,1,true);
    for(const auto& [id,tex]:g_textures)image.textures.emplace(id,RenderImage::Tex{tex,read_texture(tex.tex,tex.levels)});
    for(const auto& x:g_xfbs)image.displays.push_back({x.base,x.stride,x.h,x.written_rows,x.written,read_texture(x.tex,1)});
    checkpoint_gl_ok();return image;
}
static void write_render_image(const RenderImage& image) {
    if(g_scale!=image.scale)throw std::logic_error("Internal resolution changed across a render checkpoint");
    checkpoint_texture_access();
    write_texture(g_efb_color,image.color);write_texture(g_efb_depth,image.depth);
    g_textures.clear();
    for(const auto& [id,tex]:image.textures) {
        auto t=tex.metadata;glGenTextures(1,&t.tex);t.owner=own_texture(t.tex);write_texture(t.tex,tex.image);g_textures.emplace(id,t);
    }
    for(auto& x:g_xfbs){glDeleteTextures(1,&x.tex);glDeleteFramebuffers(1,&x.fbo);}
    g_xfbs.clear();
    for(const auto& saved:image.displays) {
        Xfb x{};x.base=saved.base;x.stride=saved.stride;x.h=saved.height;x.written_rows=saved.written_rows;x.written=saved.written;
        glGenTextures(1,&x.tex);write_texture(x.tex,saved.image);glGenFramebuffers(1,&x.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER,x.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,x.tex,0);
        g_xfbs.push_back(std::move(x));
    }
    g_render_frame=image.frame;g_present_count=image.present;restore_frames_taken(image.taken);g_dead_pending=image.dead;
    std::copy(image.zplane.begin(),image.zplane.end(),std::begin(g_zplane));
    glBindFramebuffer(GL_FRAMEBUFFER,g_efb_fbo);
    checkpoint_gl_ok();
}
// Only full-resolution EFB/XFB targets need a copy at capture. Other textures
// retain their existing GL object; do_efb_copy allocates on an outstanding lease.
struct TargetDescription {
    GLint w=0,h=0,internal=0;
    std::array<GLint,5> params{};
    bool depth=false;
    bool operator==(const TargetDescription&) const = default;
    size_t bytes()const{return size_t(w)*h*4;}
};
static constexpr GLenum target_parameters[]={GL_TEXTURE_MIN_FILTER,GL_TEXTURE_MAG_FILTER,GL_TEXTURE_WRAP_S,GL_TEXTURE_WRAP_T,GL_TEXTURE_MAX_LEVEL};
static TargetDescription describe_target(GLuint source,bool depth) {
    glBindTexture(GL_TEXTURE_2D,source);
    TargetDescription d;d.depth=depth;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&d.w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&d.h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_INTERNAL_FORMAT,&d.internal);
    for(unsigned i=0;i<5;++i)glGetTexParameteriv(GL_TEXTURE_2D,target_parameters[i],&d.params[i]);
    return d;
}
static void copy_target_known(GLuint source,GLuint target,const TargetDescription& d,bool allocate) {
    static GLuint read=0;if(!read)glGenFramebuffers(1,&read);glBindFramebuffer(GL_FRAMEBUFFER,read);
    glFramebufferTexture2D(GL_FRAMEBUFFER,d.depth?GL_COLOR_ATTACHMENT0:GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);
    glFramebufferTexture2D(GL_FRAMEBUFFER,d.depth?GL_DEPTH_ATTACHMENT:GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,source,0);
    glReadBuffer(d.depth?GL_NONE:GL_COLOR_ATTACHMENT0);glDrawBuffer(d.depth?GL_NONE:GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Checkpoint source framebuffer is incomplete");
    glBindTexture(GL_TEXTURE_2D,target);
    if(allocate)glTexImage2D(GL_TEXTURE_2D,0,d.internal,d.w,d.h,0,d.depth?GL_DEPTH_COMPONENT:GL_RGBA,
        d.depth?(d.internal==GL_DEPTH_COMPONENT32F?GL_FLOAT:GL_UNSIGNED_INT):GL_UNSIGNED_BYTE,nullptr);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,d.w,d.h);
    for(unsigned i=0;i<5;++i)glTexParameteri(GL_TEXTURE_2D,target_parameters[i],d.params[i]);
    glBindFramebuffer(GL_FRAMEBUFFER,g_efb_fbo);
}
static void copy_target(GLuint source,GLuint target,bool depth,bool allocate) {
    copy_target_known(source,target,describe_target(source,depth),allocate);
}
static GLuint clone_target(GLuint source,bool depth=false) {
    GLuint result=0;glGenTextures(1,&result);copy_target(source,result,depth,true);return result;
}
struct TargetPool {
    struct Entry {GLuint texture;TargetDescription description;};
    std::mutex mutex;
    std::vector<Entry> available;
    size_t bytes=0;
};
static TargetPool& target_pool() {static auto* pool=new TargetPool;return *pool;}
static std::shared_ptr<const GLuint> clone_target_lease(GLuint source,bool depth=false) {
    if(!getenv("GCN_RENDER_TARGET_POOL"))return own_texture(clone_target(source,depth));
    const auto description=describe_target(source,depth);GLuint target=0;
    auto& pool=target_pool();
    {
        std::lock_guard<std::mutex> lock(pool.mutex);
        auto found=std::find_if(pool.available.begin(),pool.available.end(),[&](const auto& entry){return entry.description==description;});
        if(found!=pool.available.end()){target=found->texture;pool.bytes-=description.bytes();pool.available.erase(found);}
    }
    const bool allocate=!target;if(allocate)glGenTextures(1,&target);
    copy_target_known(source,target,description,allocate);
    return std::shared_ptr<const GLuint>(new GLuint(target),[description](const GLuint* value) {
        auto& pool=target_pool();bool retained=false;
        {
            std::lock_guard<std::mutex> lock(pool.mutex);
            if(pool.available.size()<32 && pool.bytes+description.bytes()<=256u*1024u*1024u) {
                pool.available.push_back({*value,description});pool.bytes+=description.bytes();retained=true;
            }
        }
        if(!retained){auto& garbage=texture_garbage();std::lock_guard<std::mutex> lock(garbage.mutex);garbage.names.push_back(*value);}
        delete value;
    });
}
struct GpuRenderImage {
    std::shared_ptr<const GLuint> color,depth;
    std::unordered_map<uint32_t,GlTex> textures;
    struct Display {Xfb metadata;std::shared_ptr<const GLuint> image;};
    std::vector<Display> displays;
    uint32_t frame,present,taken;
    int scale;
    std::vector<uint32_t> dead;
    std::array<float,4> zplane;
};
static GpuRenderImage save_gpu_image() {
    if(g_vr_active || g_flat_eye!=0 || (g_frame_hook && !g_frame_hook_batch_only) ||
       (g_flat_overlay_hook && !g_flat_overlay_batch_only) || g_fp_on || getenv("GCN_THEATER_STEREO"))
        throw std::logic_error("Render checkpoints currently require a flat desktop view");
    checkpoint_gl_ok();checkpoint_texture_access();
    GpuRenderImage image;
    image.scale=g_scale;image.frame=g_render_frame;image.present=g_present_count;image.taken=frames_taken();image.dead=g_dead_pending;
    std::copy(std::begin(g_zplane),std::end(g_zplane),image.zplane.begin());
    image.textures=g_textures;
    image.color=clone_target_lease(g_efb_color);image.depth=clone_target_lease(g_efb_depth,true);
    for(const auto& x:g_xfbs)image.displays.push_back({x,clone_target_lease(x.tex)});
    checkpoint_gl_ok();return image;
}
static void restore_gpu_image(const GpuRenderImage& image) {
    if(g_scale!=image.scale)throw std::logic_error("Internal resolution changed across a render checkpoint");
    checkpoint_texture_access();
    copy_target(*image.color,g_efb_color,false,false);copy_target(*image.depth,g_efb_depth,true,false);
    g_textures=image.textures;
    for(auto& x:g_xfbs){glDeleteTextures(1,&x.tex);glDeleteFramebuffers(1,&x.fbo);}
    g_xfbs.clear();
    for(const auto& saved:image.displays) {
        Xfb x=saved.metadata;x.tex=clone_target(*saved.image);glGenFramebuffers(1,&x.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER,x.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,x.tex,0);
        g_xfbs.push_back(std::move(x));
    }
    g_render_frame=image.frame;g_present_count=image.present;restore_frames_taken(image.taken);g_dead_pending=image.dead;
    std::copy(image.zplane.begin(),image.zplane.end(),std::begin(g_zplane));
    glBindFramebuffer(GL_FRAMEBUFFER,g_efb_fbo);checkpoint_gl_ok();
}
static RenderImage read_saved_gpu_image(const GpuRenderImage& saved) {
    checkpoint_texture_access();
    RenderImage image;
    image.scale=saved.scale;image.frame=saved.frame;image.present=saved.present;image.taken=saved.taken;image.dead=saved.dead;image.zplane=saved.zplane;
    image.color=read_texture(*saved.color,1);image.depth=read_texture(*saved.depth,1,true);
    for(const auto& [id,tex]:saved.textures)image.textures.emplace(id,RenderImage::Tex{tex,read_texture(tex.tex,tex.levels)});
    for(const auto& d:saved.displays) {
        const auto& x=d.metadata;image.displays.push_back({x.base,x.stride,x.h,x.written_rows,x.written,read_texture(*d.image,1)});
    }
    checkpoint_gl_ok();return image;
}
static DeviceFragment render_checkpoint_on_thread() {
    if(getenv("GCN_RENDER_CPU_CHECKPOINT")) {
        auto image=std::make_shared<const RenderImage>(read_render_image());
        return {"graphics CPU readback resources",[image]{write_render_image(*image);},[image]{return read_render_image()==*image;}};
    }
    auto image=std::make_shared<const GpuRenderImage>(save_gpu_image());
    // An independent readback oracle catches accidental writes through shared
    // texture leases. Comparing two aliases of the same overwritten texture
    // would otherwise falsely certify copy-on-write.
    auto oracle=getenv("GCN_RENDER_VERIFY")?std::make_shared<const RenderImage>(read_saved_gpu_image(*image)):nullptr;
    return {"graphics GPU resources",[image,oracle]{
        if(oracle && !(read_saved_gpu_image(*image)==*oracle))throw std::runtime_error("Saved GPU texture was overwritten");
        restore_gpu_image(*image);
    },[image,oracle]{
        const auto saved=read_saved_gpu_image(*image);
        if(oracle && !(saved==*oracle))throw std::runtime_error("Saved GPU texture was overwritten");
        return read_render_image()==saved;
    }};
}
#else
static DeviceFragment render_checkpoint_on_thread() {
    throw std::logic_error("Render checkpoints require desktop GL");
}
#endif
