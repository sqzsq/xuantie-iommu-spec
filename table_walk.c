#define RSIDLEN 16
#define PRADDR_MASK ((1U << 44) - 1) 

#define OFFSET_DTBASE 24

#define DTE_V_BIT (1U << 0) 
#define DTE_S_BIT (1U << 1) 
#define DTE_F_BIT (1U << 2) 
u64 rsiddiv_reg;
u64 dtbase_reg;

void translate(rsid_t rsid, u64 input_addr, at_t access_type, u64* output_addr, int stage2, u64* reg_page, u64 s2_atp2)
{
    u64 rsid_hi;
    u64 rsid_lo;
    u64 out_dte;
    u64 dtbase; rsiddiv
    int do_stage_one = do_stage_two = 0;
    u64 s1_atp, s2_atp, dev_conf;

    if (!stage2)
    {
        dtbase = dtbase_reg;
        rsiddiv = rsiddiv_reg;
        s2_atp = s2_atp2;
    }
    else
    {
        dtbase = reg_page[3];
        rsiddiv = reg_page[1];
    }

    if (rsiddiv)
    {
        u64 rsid_hi = rsid >> rsiddiv;
        u64 rsid_lo = rsid & ((1 << rsiddiv) - 1);
        u64 lv1_dte_addr = dtbase + (rsid_hi << 3);
        u64 lv1_dte;

        if (!stage2)
        {
            lv1_dte = memory_load8 (lv1_dte_addr);
        }
        else
        {
            int res;
            u64 exp_addr;
            u64 lv1_dte_addr_phys = riscv_one_stage(s2_atp, lv1_dte_addr, ACC_READ, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
                return;
            }
            lv1_dte = memory_load8 (lv1_dte_addr_phys);
        }

        if (lv1_dte & 0x7)
        {
            raise_exception(EX_RESERVED_BIT_SET, lv1_dte);
            return;
        }

        u64 lv2_dte_addr = lv1_dte + (rsid_lo << 3);

        if (!stage2)
        {
            out_dte =  memory_load8 (lv2_dte_addr);
        }
        else
        {
            int res;
            u64 exp_addr;
            u64 lv2_dte_addr_phys = riscv_one_stage(s2_atp, lv2_dte_addr, ACC_READ, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
                return;
            }
            out_dte = memory_load8 (lv2_dte_addr_phys);
        }
    }
    else
    {
        u64 dte_addr = dtbase + (rsid << 3);

        if (!stage2)
        {
            out_dte = memory_load8 (dte_addr);
        }
        else
        {
            int res;
            u64 exp_addr;
            u64 dte_addr_phys = riscv_one_stage(s2_atp, dte_addr, ACC_READ, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
                return;
            }
            out_dte = memory_load8 (dte_addr_phys);
        }
    }

    if(out_dte & DTE_V_BIT)
    {
        *output_addr = input_addr;
        return;
    }

    u64 desc_addr = out_dte & ~0x7;
    if (!stage2)
    {
        dev_conf = memory_load8 (desc_addr + 16);
    }
    else
    {
        int res;
        u64 exp_addr;
        u64 dev_conf_phys = riscv_one_stage(s2_atp, desc_addr +16 , ACC_READ, &res, &exp_addr);
        if(!res)
        {
            raise_exception(res, exp_addr);
            return;
        }
        dev_conf = memory_load8 (desc_addr_phys);
    }

    if(out_dte & DTE_F_BIT)
    {
        do_stage_one = 1;
        if (!stage2)
        {
            s1_atp = memory_load8 (desc_addr);
        }
        else
        {
            int res;
            u64 exp_addr;
            u64 s1_atp_phys = riscv_one_stage(s2_atp, desc_addr , ACC_READ, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
                return;
            }
            s1_atp = memory_load8 (s1_atp_phys);
        }
    }

    if(out_dte & DTE_S_BIT)
    {
        if (stage2) /* only allow stage-2 in host */
        {
            raise_exception(EX_CONFIG, 0);
            return;
        }
        else
        {
            do_stage_two = 1;
            s2_atp = memory_load8 (desc_addr + 8);
        }
    }

    if(!do_stage_two)
    {
        if(!do_stage_one)
        {
            if (s1_atp & (0xF << 60)) /* S1MODE is 0 */
            {
                // do nothing for now
                // raise_exception(EX_S1MODE_INCONSISTENT, desc_addr);
                // return;
            }
            *output_addr = input_addr;
            return;
        }
        else /* Stage-2 disabled, Stage-1 enabled */
        {
            if (!stage2) /* Not the nested stage */
            {
                int res;
                u64 exp_addr;
                *output_addr = riscv_one_stage(s1_atp, input_addr, access_type, &res, &exp_addr);
                if(!res)
                {
                    raise_exception(res, exp_addr);
                }
                return;
            }
            else /* Nested stage */
            {
                int res;
                u64 exp_addr;
                *output_addr = riscv_two_stages(s1_atp, s2_atp, input_addr, access_type, &res, &exp_addr);
                if(!res)
                {
                    raise_exception(res, exp_addr);
                }
                return;
            }
        }
    }
    else
    {
        if(!do_stage_one) /* Stage-2 enabled, stage-1 disabled */
        {
            int res;
            u64 exp_addr;
            *output_addr = riscv_one_stage(s2_atp, input_addr, access_type, &res, &exp_addr);
            if(!res)
            {
                raise_exception(res, exp_addr);
            }
            return;
        }
        else /* Stage-2 enabled, stage-1 enabled */
        {
            u64 viommu_reg_addr = memory_load8 (desc_addr + OFFSET_DTBASE);
            viommu_reg_addr &= ~RPADDR_MASK;
            if(viommu_reg_addr)
            {
                raise_exception(EX_RESERVED_BIT_SET, desc_addr + OFFSET_DTBASE);
                return;
            }

            /* now we are in GPA space...  */
            u64 guest_dtbase = memory_load8 (viommu_reg_addr);

            int res;
            u64 exp_addr;
            u64 guest_dtbase_phys = riscv_one_stage(s2_atp, guest_dtbase, ACC_READ, &res, &exp_addr);

            u64 guest_rsid = (memory_load8 (desc_addr + 128) & (0xFFFFFFFFUL << 32)) >> 32;
            translate (guest_rsid, input_addr, access_type, output_addr, 1, guest_dtbase_phys);

        }
    }

    return;
}
